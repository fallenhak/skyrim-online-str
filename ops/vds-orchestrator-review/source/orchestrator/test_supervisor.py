from __future__ import annotations

import json
import os
import subprocess
import tempfile
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
