from __future__ import annotations

import json
import os
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

import supervisor


class Harness(supervisor.Supervisor):
    def __init__(self) -> None:
        self.config = {
            "repo": "example/repo",
            "max_changed_files": 40,
            "max_total_diff_bytes": 524288,
            "required_workflows": ["Build linux", "Build windows"],
            "lanes": {
                "combat": {
                    "branch": "parallel/combat-foundations",
                    "worktree": "",
                    "issue": 31,
                    "forbidden_prefixes": [],
                },
            },
        }
        self.state = {
            "global_mode": "RUNNING",
            "mode_reason": "test",
            "safety_pause": False,
            "codex_availability": {
                "status": "AVAILABLE",
                "backoff_seconds": 0,
                "retry_count": 0,
            },
            "lanes": {},
            "events": [],
        }
        self.processes = {}
        self.output_threads = {}
        self.last_save = 0.0
        self.last_resource_guard = 0.0
        self.last_git_health_check = 0.0
        self.review_calls: list[tuple[str, list[str], dict]] = []

    def env(self) -> dict[str, str]:
        return os.environ.copy()

    def event(self, message: str, lane: str | None = None) -> None:
        self.state.setdefault("events", []).append({"message": message, "lane": lane})

    def save_state(self) -> None:
        return None

    def review(self, lane_name: str, reasons: list[str], **kwargs) -> None:
        self.review_calls.append((lane_name, reasons, kwargs))
        lane = self.state["lanes"][lane_name]
        lane["state"] = "NEEDS_SOL_REVIEW"
        lane["review"] = {
            "type": kwargs.get("review_type", "CURRENT_PHASE_REVIEW"),
            "reviewed_phase": kwargs.get("reviewed_phase"),
            "commit_sha": kwargs.get("commit_sha"),
            "next_phase": kwargs.get("next_phase"),
        }
        lane["review_reasons"] = reasons


def make_git_repo() -> Path:
    root = Path(tempfile.mkdtemp(prefix="skyrim-supervisor-test-"))
    subprocess.run(["git", "init", "-q", "-b", "main", str(root)], check=True)
    (root / "tracked.txt").write_text("base\n", encoding="utf-8")
    subprocess.run(["git", "-C", str(root), "add", "tracked.txt"], check=True)
    subprocess.run([
        "git", "-C", str(root), "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
        "commit", "-qm", "initial",
    ], check=True)
    return root


def configure_combat_lane(harness: Harness, root: Path, state: str = "LOCAL_VALIDATION") -> dict:
    harness.config["lanes"]["combat"]["branch"] = "main"
    harness.config["lanes"]["combat"]["worktree"] = str(root)
    lane = {
        "state": state,
        "branch": "main",
        "worktree": str(root),
        "plan": "PLAN.md",
        "phase_index": 0,
        "phase_id": "C01",
        "phase_title": "phase",
        "review": {},
        "review_reasons": [],
        "worker_log": None,
        "history": [],
        "validation": {},
        "ci": {},
        "last_commit": None,
        "success_since_review": 0,
        "phase_failure_count": 0,
        "recovery_attempts": 0,
        "push_attempts": 0,
        "ci_poll_failures": 0,
    }
    harness.state["lanes"]["combat"] = lane
    return lane


class SupervisorLogicTests(unittest.TestCase):
    def test_required_workflows_all_success(self) -> None:
        runs = [
            {"workflowName": "Build windows", "headSha": "abc", "status": "completed", "conclusion": "success", "databaseId": 2},
            {"workflowName": "Build linux", "headSha": "abc", "status": "completed", "conclusion": "success", "databaseId": 1},
        ]
        result = supervisor.aggregate_required_workflows(runs, ["Build linux", "Build windows"], "abc")
        self.assertEqual(result["status"], "PASS")

    def test_required_workflows_partial_completion_waits(self) -> None:
        for running_name in ("Build linux", "Build windows"):
            other = "Build windows" if running_name == "Build linux" else "Build linux"
            result = supervisor.aggregate_required_workflows([
                {"workflowName": running_name, "headSha": "abc", "status": "in_progress", "databaseId": 2},
                {"workflowName": other, "headSha": "abc", "status": "completed", "conclusion": "success", "databaseId": 1},
            ], ["Build linux", "Build windows"], "abc")
            self.assertEqual(result["status"], "WAIT")

    def test_required_workflows_any_failure_fails(self) -> None:
        for failed_name in ("Build linux", "Build windows"):
            other = "Build windows" if failed_name == "Build linux" else "Build linux"
            result = supervisor.aggregate_required_workflows([
                {"workflowName": failed_name, "headSha": "abc", "status": "completed", "conclusion": "failure", "databaseId": 2},
                {"workflowName": other, "headSha": "abc", "status": "completed", "conclusion": "success", "databaseId": 1},
            ], ["Build linux", "Build windows"], "abc")
            self.assertEqual(result["status"], "FAIL")

    def test_required_workflow_absent_waits(self) -> None:
        result = supervisor.aggregate_required_workflows([
            {"workflowName": "Build linux", "headSha": "abc", "status": "completed", "conclusion": "success", "databaseId": 1},
        ], ["Build linux", "Build windows"], "abc")
        self.assertEqual(result["status"], "WAIT")
        self.assertEqual(result["missing"], ["Build windows"])

    def test_newer_attempt_wins(self) -> None:
        result = supervisor.aggregate_required_workflows([
            {"workflowName": "Build linux", "headSha": "abc", "status": "completed", "conclusion": "failure", "databaseId": 3, "updatedAt": "2026-01-02T00:00:00Z"},
            {"workflowName": "Build linux", "headSha": "abc", "status": "completed", "conclusion": "success", "databaseId": 4, "updatedAt": "2026-01-03T00:00:00Z"},
            {"workflowName": "Build windows", "headSha": "abc", "status": "completed", "conclusion": "success", "databaseId": 5},
        ], ["Build linux", "Build windows"], "abc")
        self.assertEqual(result["status"], "PASS")
        linux = next(item for item in result["required"] if item["workflow"] == "Build linux")
        self.assertEqual(linux["run_id"], 4)

    def test_exact_third_success_checkpoint(self) -> None:
        self.assertFalse(supervisor.checkpoint_due(2))
        self.assertTrue(supervisor.checkpoint_due(3))

    def test_complete_phase_requests_review_on_third_success_before_fourth(self) -> None:
        h = Harness()
        phases = [
            {"id": "C01", "title": "one"},
            {"id": "C02", "title": "two"},
            {"id": "C03", "title": "three"},
            {"id": "C04", "title": "four"},
        ]
        h.state["lanes"]["combat"] = {
            "state": "WAITING_FOR_CI",
            "phase_index": 0,
            "phase_id": "C01",
            "phase_title": "one",
            "success_since_review": 0,
            "phase_failure_count": 0,
            "recovery_attempts": 0,
            "push_attempts": 0,
            "ci_poll_failures": 0,
            "validation": {},
            "ci": {},
            "history": [],
            "last_commit": "sha",
            "review": {},
        }
        h.plan_for = lambda lane: {"phases": phases, "boundaries": []}
        h.review_reasons = lambda lane: []
        def refresh(_lane: str, state: dict) -> None:
            index = state["phase_index"]
            state["phase_id"] = phases[index]["id"] if index < len(phases) else None
            state["phase_title"] = phases[index]["title"] if index < len(phases) else None
        h._refresh_plan = refresh
        h.complete_phase("combat")
        h.complete_phase("combat")
        self.assertEqual(h.state["lanes"]["combat"]["success_since_review"], 2)
        h.complete_phase("combat")
        self.assertEqual(h.state["lanes"]["combat"]["success_since_review"], 3)
        self.assertEqual(h.state["lanes"]["combat"]["phase_index"], 2)
        self.assertEqual(h.review_calls[-1][2]["review_type"], "POST_PHASE_CHECKPOINT")

    def test_post_phase_approval_advances_once_and_resets_counter(self) -> None:
        h = Harness()
        h.state["lanes"]["combat"] = {
            "state": "NEEDS_SOL_REVIEW",
            "phase_index": 2,
            "phase_id": "C03",
            "phase_title": "lifecycle",
            "success_since_review": 3,
            "review_reasons": ["checkpoint"],
            "review": {
                "type": "POST_PHASE_CHECKPOINT",
                "reviewed_phase": {"id": "C03", "title": "lifecycle"},
                "commit_sha": "deadbeef",
                "next_phase": {"id": "C04", "title": "next"},
            },
            "worktree": "",
            "branch": "parallel/combat-foundations",
        }
        h.plan_for = lambda lane: {"phases": [
            {"id": "C03", "title": "lifecycle"}, {"id": "C04", "title": "next"}
        ], "boundaries": []}
        h._refresh_plan = lambda lane, state: state.update({"phase_id": "C04", "phase_title": "next"})
        self.assertEqual(h.approve_review("combat"), 0)
        self.assertEqual(h.state["lanes"]["combat"]["phase_index"], 3)
        self.assertEqual(h.state["lanes"]["combat"]["success_since_review"], 0)
        self.assertEqual(h.state["lanes"]["combat"]["state"], "READY")
        self.assertEqual(h.approve_review("combat"), 1)
        self.assertEqual(h.state["lanes"]["combat"]["phase_index"], 3)

    def test_current_phase_review_cannot_be_approved(self) -> None:
        h = Harness()
        h.state["lanes"]["combat"] = {
            "state": "NEEDS_SOL_REVIEW",
            "phase_index": 2,
            "phase_id": "C03",
            "phase_title": "lifecycle",
            "review": {"type": "CURRENT_PHASE_REVIEW"},
            "review_reasons": ["worker failed"],
        }
        self.assertEqual(h.approve_review("combat"), 1)

    def test_porcelain_status_detects_staged_untracked_deletion_rename(self) -> None:
        root = make_git_repo()
        harness = object.__new__(supervisor.Supervisor)
        harness.env = lambda: os.environ.copy()
        harness.command = supervisor.Supervisor.command.__get__(harness, supervisor.Supervisor)
        (root / "tracked.txt").write_text("unstaged\n", encoding="utf-8")
        (root / "modified-only.txt").write_text("before\n", encoding="utf-8")
        (root / "staged.txt").write_text("staged\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(root), "add", "staged.txt"], check=True)
        (root / "deleted.txt").write_text("delete\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(root), "add", "deleted.txt"], check=True)
        subprocess.run(["git", "-C", str(root), "add", "modified-only.txt"], check=True)
        subprocess.run([
            "git", "-C", str(root), "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
            "commit", "-qm", "more",
        ], check=True)
        (root / "modified-only.txt").write_text("after\n", encoding="utf-8")
        (root / "deleted.txt").unlink()
        (root / "renamed.txt").write_text("renamed\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(root), "mv", "tracked.txt", "renamed-tracked.txt"], check=True)
        ok, entries, error = harness.git_status_details(str(root))
        self.assertTrue(ok, error)
        kinds = {entry["kind"] for entry in entries}
        self.assertIn("deletion", kinds)
        self.assertIn("rename", kinds)
        self.assertIn("untracked", kinds)
        self.assertIn("modified", kinds)

    def test_git_status_failure_is_dirty_and_status_files_is_not_clean(self) -> None:
        h = Harness()
        h.git_status_details = lambda _worktree: (False, [], "git status failed")
        self.assertTrue(h.git_dirty("/missing/worktree"))
        self.assertIsNone(h.git_status_files("/missing/worktree"))

    def test_pre_worker_status_failure_is_rejected(self) -> None:
        root = make_git_repo()
        h = Harness()
        lane = configure_combat_lane(h, root, "READY")
        h.git_status_details = lambda _worktree: (False, [], "git status unavailable")
        okay, reason = h.verify_worker_worktree("combat", recovery=False)
        self.assertFalse(okay)
        self.assertIn("status unavailable", reason)
        self.assertEqual(lane["state"], "READY")

    def test_post_commit_status_failure_prohibits_automatic_push(self) -> None:
        root = make_git_repo()
        (root / "tracked.txt").write_text("changed\n", encoding="utf-8")
        h = Harness()
        lane = configure_combat_lane(h, root, "COMMITTING")
        lane["validation"] = {"changed_files": ["tracked.txt"], "tests": []}
        calls = 0
        real_status = h.git_status_details

        def fail_after_commit(worktree: str):
            nonlocal calls
            calls += 1
            if calls >= 2:
                return False, [], "status unavailable after commit"
            return real_status(worktree)

        h.git_status_details = fail_after_commit
        self.assertFalse(h.commit_phase("combat"))
        self.assertEqual(lane["state"], "NEEDS_SOL_REVIEW")
        self.assertTrue(any("automatic push is forbidden" in reason for reason in h.review_calls[-1][1]))

    def test_pre_push_status_failure_prohibits_push(self) -> None:
        root = make_git_repo()
        h = Harness()
        lane = configure_combat_lane(h, root, "PUSHING")
        lane["last_commit"] = "initial"
        h.git_status_details = lambda _worktree: (False, [], "status unavailable before push")
        pushed = []

        def record_command(args, *argspec, **kwargs):
            if "push" in args:
                pushed.append(args)
            return (0, "", "")

        h.command = record_command
        h.push_phase("combat")
        self.assertEqual(pushed, [])
        self.assertEqual(lane["state"], "NEEDS_SOL_REVIEW")
        self.assertIn("not clean", h.review_calls[-1][1][0])

    def test_startup_reconciles_stale_rate_limit_probe_without_spending_recovery(self) -> None:
        h = Harness()
        h.state["codex_availability"] = {
            "status": "RATE_LIMITED",
            "next_retry_at": "1970-01-01T00:00:00+00:00",
            "backoff_seconds": 1800,
            "retry_count": 4,
            "probe_started_at": "2026-09-21T15:00:00+00:00",
            "reason": "Codex usage limit reached",
        }
        h.state["lanes"]["combat"] = {
            "state": "READY", "worker_pid": 731, "recovery_attempts": 0,
        }
        h.processes = {}
        before = time.time()
        h._reconcile_startup_runtime()
        availability = h.state["codex_availability"]
        self.assertEqual(availability["status"], "RATE_LIMITED")
        self.assertIsNone(availability["probe_started_at"])
        self.assertEqual(availability["backoff_seconds"], 1800)
        self.assertEqual(availability["retry_count"], 4)
        self.assertGreater(supervisor.parse_time(availability["next_retry_at"]), before)
        self.assertIsNone(h.state["lanes"]["combat"]["worker_pid"])
        self.assertEqual(h.state["lanes"]["combat"]["recovery_attempts"], 0)

    def test_startup_preserves_future_rate_limit_retry(self) -> None:
        future = supervisor.dt.datetime.fromtimestamp(
            time.time() + 3600, supervisor.dt.timezone.utc
        ).isoformat(timespec="seconds")
        h = Harness()
        h.state["codex_availability"] = {
            "status": "RATE_LIMITED",
            "next_retry_at": future,
            "backoff_seconds": 900,
            "retry_count": 2,
            "probe_started_at": "2026-09-21T15:00:00+00:00",
        }
        h.state["lanes"]["combat"] = {"state": "READY", "recovery_attempts": 0}
        h.processes = {}
        h._reconcile_startup_runtime()
        calls = []
        h.start_worker = lambda lane_name, recovery=False: calls.append((lane_name, recovery)) or True
        h.schedule()
        self.assertEqual(calls, [])
        self.assertEqual(h.state["codex_availability"]["next_retry_at"], future)

    def test_retry_current_phase_clean_running_is_recovering(self) -> None:
        root = make_git_repo()
        h = Harness()
        lane = configure_combat_lane(h, root, "NEEDS_SOL_REVIEW")
        lane.update({
            "last_error": "second CI failure",
            "review_reasons": ["same phase failed required CI twice"],
            "review": {"type": "CURRENT_PHASE_REVIEW"},
        })
        h.state["global_mode"] = "RUNNING"
        self.assertEqual(h.retry_review("combat"), 0)
        self.assertEqual(lane["state"], "RECOVERING")
        self.assertEqual(lane["recovery_context"]["last_error"], "second CI failure")

    def test_retry_current_phase_paused_restores_recovering_on_resume(self) -> None:
        root = make_git_repo()
        h = Harness()
        lane = configure_combat_lane(h, root, "NEEDS_SOL_REVIEW")
        lane.update({
            "last_error": "worker tool failure",
            "review_reasons": ["worker failed"],
            "review": {"type": "CURRENT_PHASE_REVIEW"},
        })
        h.state["global_mode"] = "PAUSED"
        self.assertEqual(h.retry_review("combat"), 0)
        self.assertEqual(lane["state"], "PAUSED")
        self.assertEqual(lane["paused_from_state"], "RECOVERING")
        with patch.object(supervisor, "read_json", return_value={
            "desired_mode": "RUNNING", "reason": "operator resumed",
        }):
            h.refresh_control()
        self.assertEqual(h.state["global_mode"], "RUNNING")
        self.assertEqual(lane["state"], "RECOVERING")

    def test_recovery_prompt_contains_bounded_saved_failure_context(self) -> None:
        root = make_git_repo()
        log = root / "worker.log"
        log.write_text(
            "worker observed compiler failure; secret=super-secret\n",
            encoding="utf-8",
        )
        h = Harness()
        lane = configure_combat_lane(h, root, "RECOVERING")
        lane.update({
            "phase_id": "C01", "phase_title": "phase",
            "last_error": "required CI workflow failure: Build linux",
            "ci": {"failure_excerpt": "error: CharacterId authority check failed"},
            "worker_log": str(log),
        })
        h.plan_for = lambda _lane: {"phases": [{"id": "C01", "title": "phase"}], "boundaries": []}
        h.roadmap_task = lambda _lane: None
        recovery = h.worker_prompt("combat", recovery=True)
        self.assertIn("RECOVERY CONTEXT", recovery)
        self.assertIn("required CI workflow failure: Build linux", recovery)
        self.assertIn("CharacterId authority check failed", recovery)
        self.assertIn("worker observed compiler failure", recovery)
        self.assertNotIn("super-secret", recovery)
        self.assertIn("same failed phase", recovery)
        normal = h.worker_prompt("combat", recovery=False)
        self.assertNotIn("RECOVERY CONTEXT", normal)
        self.assertNotIn("Build linux", normal)
        self.assertNotIn("CharacterId authority check failed", normal)

    def test_untracked_source_content_is_in_diff_and_triggers_sol_review(self) -> None:
        root = make_git_repo()
        (root / "new_server.cpp").write_text(
            "bool accepts(CharacterId id) { return id != 0; }\n",
            encoding="utf-8",
        )
        h = Harness()
        lane = configure_combat_lane(h, root, "WAITING_FOR_CI")
        files, diff, _stat, metrics = h.changed_diff("combat")
        self.assertIn("new_server.cpp", files)
        self.assertIn("CharacterId", diff)
        self.assertGreater(metrics["untracked_review_bytes"], 0)
        lane["validation"] = {"changed_files": files, "diff_text": diff}
        h.plan_for = lambda _lane: {
            "phases": [
                {"id": "C01", "title": "phase"},
                {"id": "C02", "title": "next"},
            ],
            "boundaries": [],
        }
        h.complete_phase("combat")
        self.assertEqual(h.review_calls[-1][2]["review_type"], "POST_PHASE_CHECKPOINT")
        self.assertTrue(any("CharacterId" in reason for reason in h.review_calls[-1][1]))

    def test_small_untracked_source_is_included_as_new_file_diff(self) -> None:
        root = make_git_repo()
        (root / "new_source.py").write_text("def answer():\n    return 42\n", encoding="utf-8")
        h = Harness()
        configure_combat_lane(h, root)
        files, diff, stat, metrics = h.changed_diff("combat")
        self.assertEqual(files, ["new_source.py"])
        self.assertIn("--- /dev/null", diff)
        self.assertIn("+++ b/new_source.py", diff)
        self.assertIn("+    return 42", diff)
        self.assertIn("new_source.py", stat)
        self.assertGreaterEqual(metrics["total_diff_bytes"], len("def answer():\n    return 42\n"))

    def test_binary_and_oversize_untracked_files_are_omitted_and_blocked(self) -> None:
        root = make_git_repo()
        (root / "new_asset.bin").write_bytes(b"A" * 9000 + b"\x00binary-secret-content")
        (root / "oversize_source.cpp").write_bytes(
            b"A" * (supervisor.MAX_UNTRACKED_REVIEW_FILE_BYTES + 1)
        )
        h = Harness()
        lane = configure_combat_lane(h, root)
        files, diff, _stat, metrics = h.changed_diff("combat")
        self.assertEqual(set(files), {"new_asset.bin", "oversize_source.cpp"})
        self.assertNotIn("binary-secret-content", diff)
        self.assertNotIn("A" * 1000, diff)
        self.assertGreaterEqual(len(metrics["untracked_review_issues"]), 2)
        self.assertFalse(h.local_validate("combat"))
        self.assertEqual(lane["state"], "NEEDS_SOL_REVIEW")
        self.assertTrue(any("untracked" in reason for reason in h.review_calls[-1][1]))

    def test_stale_worker_marker_is_not_reused(self) -> None:
        h = Harness()
        root = Path(tempfile.mkdtemp(prefix="skyrim-worker-log-"))
        old = root / "old.log"
        current = root / "current.log"
        old.write_text("WORKER_RESULT: COMPLETE\n", encoding="utf-8")
        current.write_text("worker exited without marker\n", encoding="utf-8")
        h.state["lanes"]["combat"] = {"worker_log": str(current)}
        self.assertIsNone(h.worker_result("combat"))

    def test_no_change_reason_is_hard_review(self) -> None:
        root = make_git_repo()
        h = Harness()
        h.config["lanes"]["combat"]["branch"] = "main"
        h.config["lanes"]["combat"]["worktree"] = str(root)
        h.state["lanes"]["combat"] = {
            "state": "LOCAL_VALIDATION", "branch": "main", "worktree": str(root),
            "phase_id": "C03", "phase_title": "phase", "review": {},
            "worker_log": None,
        }
        self.assertFalse(h.local_validate("combat"))
        self.assertEqual(h.state["lanes"]["combat"]["state"], "NEEDS_SOL_REVIEW")
        self.assertIn("no reviewable changes", h.review_calls[-1][1][0])

    def test_diff_file_bound_requests_review(self) -> None:
        root = make_git_repo()
        (root / "tracked.txt").write_text("changed\n", encoding="utf-8")
        h = Harness()
        h.config["max_changed_files"] = 0
        h.config["lanes"]["combat"]["branch"] = "main"
        h.config["lanes"]["combat"]["worktree"] = str(root)
        h.state["lanes"]["combat"] = {
            "state": "LOCAL_VALIDATION", "branch": "main", "worktree": str(root),
            "phase_id": "C03", "phase_title": "phase", "review": {},
            "worker_log": None,
        }
        self.assertFalse(h.local_validate("combat"))
        self.assertTrue(any("exceeds configured limit" in item for item in h.review_calls[-1][1]))

    def test_pause_blocks_mutating_states(self) -> None:
        h = Harness()
        h.state["global_mode"] = "PAUSED"
        h.state["lanes"]["combat"] = {"state": "COMMITTING"}
        called = []
        h.commit_phase = lambda lane: called.append(lane) or True
        h.advance_lane("combat")
        self.assertEqual(called, [])
        self.assertEqual(h.state["lanes"]["combat"]["state"], "PAUSED")

    def test_codex_rate_limit_classifier_is_conservative(self) -> None:
        self.assertTrue(supervisor.classify_codex_usage_limit(1, "Codex usage limit reached; resets soon"))
        self.assertTrue(supervisor.classify_codex_usage_limit(1, "ChatGPT rate limit; try again later"))
        self.assertFalse(supervisor.classify_codex_usage_limit(1, "GitHub API rate limit exceeded"))
        self.assertFalse(supervisor.classify_codex_usage_limit(1, "compiler: rate limit in local parser"))

    def test_backoff_is_bounded_and_persistent_shape_is_serializable(self) -> None:
        values = [supervisor.next_codex_backoff(0), supervisor.next_codex_backoff(900), supervisor.next_codex_backoff(3600)]
        self.assertEqual(values, [900, 1800, 3600])
        state = {"codex_availability": {"status": "RATE_LIMITED", "backoff_seconds": 1800}}
        self.assertEqual(json.loads(json.dumps(state))["codex_availability"]["status"], "RATE_LIMITED")

    def test_automatic_retry_attempts_only_one_pending_lane(self) -> None:
        h = Harness()
        h.state["codex_availability"] = {
            "status": "RATE_LIMITED", "next_retry_at": "1970-01-01T00:00:00+00:00",
            "backoff_seconds": 900, "retry_count": 1, "probe_started_at": None,
        }
        h.state["lanes"]["combat"] = {"state": "READY"}
        h.state["lanes"]["authority"] = {"state": "READY"}
        h.processes = {}
        calls = []
        h.start_worker = lambda lane_name, recovery=False: calls.append((lane_name, recovery)) or True
        h.schedule()
        self.assertEqual(calls, [("combat", False)])

    def test_resource_pause_stops_workers_and_requires_clearance(self) -> None:
        class FakeProcess:
            pid = 7
            def __init__(self) -> None:
                self.stopped = False
            def terminate(self) -> None:
                self.stopped = True
            def wait(self, timeout: int) -> None:
                return None

        h = Harness()
        process = FakeProcess()
        h.processes = {"combat": process}
        h.state["lanes"]["combat"] = {"state": "CODING", "worker_pid": process.pid}
        h.pause_for_safety("memory pressure")
        self.assertTrue(process.stopped)
        self.assertEqual(h.state["global_mode"], "PAUSED")
        self.assertTrue(h.state["safety_pause"])
        self.assertEqual(h.state["lanes"]["combat"]["state"], "PAUSED")

    def test_diff_bound_helper_inputs(self) -> None:
        self.assertTrue(supervisor.generated_path("build/output.o"))
        self.assertTrue(supervisor.generated_path("node_modules/x"))
        self.assertTrue(supervisor.generated_path("dist/file"))
        self.assertFalse(supervisor.generated_path("Code/server/file.cpp"))


if __name__ == "__main__":
    unittest.main()
