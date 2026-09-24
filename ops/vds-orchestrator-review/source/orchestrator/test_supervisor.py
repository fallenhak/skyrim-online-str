from __future__ import annotations

import io
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from copy import deepcopy
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import supervisor
import architect_review


class Harness(supervisor.Supervisor):
    def __init__(self) -> None:
        self.runtime_owner = True
        self._state_write_enabled = True
        self._deferred_save_depth = 0
        self._save_deferred = False
        self.config = {
            "repo": "example/repo",
            "max_changed_files": 40,
            "max_total_diff_bytes": 524288,
            "required_workflows": ["Build linux", "Build windows"],
            "model_routing": {
                "review": {
                    "normal": {
                        "model": "gpt-6-luna",
                        "family": "luna",
                        "reasoning_effort": "max",
                    },
                },
            },
            "operator_request_root": tempfile.mkdtemp(prefix="skyrim-operator-requests-"),
            "max_processed_operator_requests": 3,
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
            "processed_operator_requests": {},
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

    def save_state(self) -> bool:
        return True

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


class DurableSaveHarness(Harness):
    """Harness with a small persisted-state model for request durability tests."""

    def __init__(self) -> None:
        super().__init__()
        self.persisted_state = deepcopy(self.state)
        self.persisted_roadmap_snapshot = None
        self.fail_next_save = False
        self.save_calls = 0

    def save_state(self) -> bool:
        if self._deferred_save_depth:
            self._save_deferred = True
            return True
        self.save_calls += 1
        if self.fail_next_save:
            self.fail_next_save = False
            raise OSError("simulated durable state save failure")
        self.persisted_state = deepcopy(self.state)
        self.persisted_roadmap_snapshot = deepcopy(
            getattr(self, "roadmap_snapshot", None)
        )
        self._save_deferred = False
        return True


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


def operator_payload(request_id: str, command: str, args: dict) -> dict:
    return {
        "version": supervisor.OPERATOR_REQUEST_VERSION,
        "request_id": request_id,
        "command": command,
        "args": args,
        "created_at": supervisor.utc_now(),
        "submitted_by": "test",
    }


def put_operator_request(harness: Harness, payload: dict) -> Path:
    paths = supervisor.ensure_operator_request_dirs(harness.config)
    path = paths["inbox"] / f"{payload['request_id']}.json"
    supervisor.atomic_write_json(path, payload, 0o600)
    return path


def ownership_snapshot(harness: supervisor.Supervisor) -> dict:
    lane_fields = (
        "state", "worker_pid", "worker_started_at", "last_progress_at",
        "recovery_attempts", "worker_log", "worker_recovery", "worker_attempt",
    )
    return {
        "lanes": {
            lane_name: {key: harness.state["lanes"][lane_name].get(key) for key in lane_fields}
            for lane_name in harness.state["lanes"]
        },
        "codex_availability": dict(harness.state.get("codex_availability", {})),
        "scheduler_tasks": {
            task_id: dict(record)
            for task_id, record in harness.state.get("scheduler", {}).get("tasks", {}).items()
            if isinstance(record, dict)
        },
        "runtime_identity": harness.state.get("runtime_identity"),
    }


class LiveWorkerObserverHarness(Harness):
    def __init__(self) -> None:
        super().__init__()
        self.runtime_owner = False
        self._state_write_enabled = False
        self.config["worker_gh_config_dir"] = tempfile.mkdtemp(prefix="skyrim-gh-empty-")
        self.config["lanes"] = {
            lane_name: {
                "branch": f"parallel/{lane_name}",
                "worktree": "",
                "issue": 0,
                "plan": "PLAN.md",
                "forbidden_prefixes": [],
            }
            for lane_name in supervisor.LANE_ORDER
        }
        pids = {"combat": 111, "authority": 222, "population": 333, "ui": 444}
        self.state["lanes"] = {
            lane_name: {
                "state": "CODING",
                "worker_pid": pid,
                "worker_started_at": "2026-09-22T00:00:00+00:00",
                "last_progress_at": "2026-09-22T00:01:00+00:00",
                "recovery_attempts": 0,
                "worker_log": f"/var/log/skyrim-dev/{lane_name}.log",
                "worker_recovery": False,
                "worker_attempt": 1,
                "branch": f"parallel/{lane_name}",
                "worktree": "",
                "phase_id": f"{lane_name[:1].upper()}01",
                "phase_title": "live phase",
                "review": {},
                "review_reasons": [],
            }
            for lane_name, pid in pids.items()
        }
        self.state["codex_availability"] = {
            "status": "RATE_LIMITED",
            "probe_started_at": "2026-09-22T00:01:10+00:00",
            "next_retry_at": "2026-09-22T00:15:00+00:00",
            "backoff_seconds": 900,
            "retry_count": 2,
            "reason": "live probe",
        }
        self.state["scheduler"] = {
            "tasks": {
                "C03": {
                    "state": "RUNNING", "worker_pid": 111,
                    "started_at": "2026-09-22T00:00:00+00:00",
                },
            },
            "approvals": {}, "reviews": {}, "workstreams": {}, "milestones": {},
            "resolved_external_gates": [], "audit": [],
        }
        self.state["runtime_identity"] = {"daemon_pid": 900, "lock": "held"}
        self.roadmap_snapshot = SimpleNamespace(
            current_milestone="M01", milestones={}, workstreams={}, tasks={}
        )

    def _recompute_scheduler(self) -> None:
        return None

    def _refresh_plan(self, lane_name: str, lane: dict) -> None:
        lane["plan_data"] = {
            "phases": [{"id": lane.get("phase_id"), "title": lane.get("phase_title")}],
            "boundaries": [],
        }

    def sync_control_plane(self, force: bool = False) -> bool:
        return True

    def resource_guard(self) -> tuple[bool, str]:
        return True, ""

    def git_health(self) -> tuple[bool, str]:
        return True, ""

    def git_dirty(self, worktree: str) -> bool:
        return False

    def command(self, args, cwd=None, timeout=120):
        if args[:2] == ["systemctl", "is-active"]:
            return 0, "inactive\n", ""
        if args[:2] == ["codex", "--version"]:
            return 0, "codex-cli 0.155.1\n", ""
        if args[:2] == ["codex", "login"]:
            return 0, "Logged in\n", ""
        if args[:2] == ["gh", "auth"]:
            return 0, "authenticated\n", ""
        if args[:2] == ["gh", "api"]:
            return 0, "true\n", ""
        if args[:2] == ["gh", "run"]:
            return 0, "[]\n", ""
        if "push" in args:
            return 0, "", ""
        return 0, "", ""


class TerminalReviewSchedulerHarness(Harness):
    def __init__(self) -> None:
        super().__init__()
        self._control_plane_valid = lambda: True
        self.roadmap_snapshot = SimpleNamespace(
            workstreams={
                lane: {"id": f"existing-{lane}", "mode": "EXISTING_PLAN", "lane": lane,
                       "milestone_id": "M01", "external_gates": []}
                for lane in supervisor.LANE_ORDER
            },
            milestones={"M01": {"status": "ACTIVE", "executable": True}},
            tasks={},
        )
        self.config["lanes"] = {lane: {"branch": f"parallel/{lane}", "worktree": f"/tmp/{lane}"}
                                 for lane in supervisor.LANE_ORDER}
        self.state.update({"global_mode": "RUNNING", "control_plane": {"applied_sha": "c" * 40, "status": "VALID"}})
        self.state["scheduler"] = {"tasks": {}, "approvals": {}, "reviews": {}, "workstreams": {},
                                    "milestones": {}, "resolved_external_gates": [], "audit": []}
        self.state["lanes"] = {}
        for lane in supervisor.LANE_ORDER:
            phase = {"combat": "C01", "authority": "A01", "population": "L01", "ui": "U01"}[lane]
            state = "BLOCKED" if lane == "combat" else "NEEDS_SOL_REVIEW"
            sha = "a" * 40 if lane == "combat" else "b" * 40
            self.state["lanes"][lane] = {
                "state": state, "phase_id": phase, "phase_title": phase,
                "phase_index": 0, "last_commit": sha,
                "review": ({"type": "POST_PHASE_CHECKPOINT", "decision": "SOL_BLOCKED", "commit_sha": sha}
                           if lane == "combat" else {"type": "CURRENT_PHASE_REVIEW", "commit_sha": sha}),
                "plan_data": {"phases": [{"id": phase, "title": phase}], "boundaries": []},
            }


class SchedulingHarness(Harness):
    def __init__(self, lane_states: dict[str, str], task_states: dict[str, str]) -> None:
        super().__init__()
        self.config["max_concurrent_workers"] = 2
        self.start_calls: list[tuple[str, bool]] = []
        self.state["global_mode"] = "RUNNING"
        self.state["scheduler"] = {
            "cursor": 0,
            "tasks": {},
            "approvals": {},
            "reviews": {},
            "workstreams": {},
            "milestones": {},
            "resolved_external_gates": [],
            "audit": [],
        }
        self.state["lanes"] = {}
        for lane_name in supervisor.LANE_ORDER:
            self.state["lanes"][lane_name] = {
                "state": lane_states.get(lane_name, "NEEDS_SOL_REVIEW"),
                "phase_id": lane_name[:1].upper() + "01",
                "phase_title": "test phase",
                "review": (
                    {"type": "POST_PHASE_CHECKPOINT"}
                    if lane_states.get(lane_name) == "NEEDS_SOL_REVIEW"
                    else {}
                ),
            }
            task_id = self.state["lanes"][lane_name]["phase_id"]
            self.state["scheduler"]["tasks"][task_id] = {
                "task_id": task_id,
                "lane": lane_name,
                "source": "EXISTING_PLAN",
                "state": task_states.get(lane_name, "PAUSED"),
                "reason": "fixture",
            }

    def _recompute_scheduler(self) -> None:
        return None

    def _control_plane_valid(self) -> bool:
        return True

    def _scheduled_task_for_lane(self, lane_name: str) -> dict | None:
        phase_id = self.state["lanes"][lane_name].get("phase_id")
        return self.state["scheduler"]["tasks"].get(phase_id)

    def start_worker(self, lane_name: str, recovery: bool = False) -> bool:
        self.start_calls.append((lane_name, recovery))
        self.processes[lane_name] = SimpleNamespace(pid=lane_name)
        return True


class ProductionPathHarness(SchedulingHarness):
    """Run the real control/advance/schedule/start admission path with fake PIDs."""

    def __init__(self) -> None:
        super().__init__(
            {lane_name: "PAUSED" for lane_name in supervisor.LANE_ORDER},
            {lane_name: "READY" for lane_name in supervisor.LANE_ORDER},
        )
        self.worker_root = Path(tempfile.mkdtemp(prefix="skyrim-worker-admission-"))
        self.state["global_mode"] = "PAUSED"
        for lane_name in supervisor.LANE_ORDER:
            lane = self.state["lanes"][lane_name]
            lane.update({
                "branch": f"parallel/{lane_name}",
                "worktree": str(self.worker_root / lane_name),
                "plan": "PLAN.md",
                "phase_index": 0,
                "plan_data": {
                    "phases": [{"id": lane["phase_id"], "title": "test phase"}],
                    "boundaries": [],
                },
                "recovery_attempts": 0,
                "worker_attempt": 0,
            })
            if lane_name == "authority":
                lane.pop("paused_from_state", None)
            else:
                lane["paused_from_state"] = "RECOVERING"
        self.start_calls = []
        self.verify_worker_worktree = lambda _lane_name, _recovery: (True, "")
        self.worker_prompt = lambda _lane_name, recovery=False: "bounded test worker"
        self._new_worker_log = lambda lane_name, _phase_id, attempt: (
            self.worker_root / f"{lane_name}-{attempt}.log"
        )
        self._prune_worker_logs = lambda _lane_name: None
        self.git_head = lambda _worktree: "a" * 40
        self.sync_control_plane = lambda force=False: True
        self.periodic_guards = lambda: True
        self.git_dirty = lambda _worktree: False
        self._control_plane_valid = lambda: True

    def start_worker(self, lane_name: str, recovery: bool = False) -> bool:
        self.start_calls.append((lane_name, recovery))
        return supervisor.Supervisor.start_worker(self, lane_name, recovery)


class FakeWorkerProcess:
    _next_pid = 10000

    def __init__(self) -> None:
        type(self)._next_pid += 1
        self.pid = type(self)._next_pid
        self.stdout = io.StringIO()
        self.returncode = None

    def poll(self):
        return self.returncode


class SupervisorLogicTests(unittest.TestCase):
    def test_operator_request_contract_rejects_unknown_and_stale_requests(self) -> None:
        unknown = operator_payload("unknown-1", "delete-all", {})
        valid, request_id, reason = supervisor.validate_operator_request(unknown)
        self.assertFalse(valid)
        self.assertEqual(request_id, "unknown-1")
        self.assertIn("unsupported", reason)
        stale = operator_payload("stale-1", "block", {"lane": "combat"})
        stale["created_at"] = "2020-01-01T00:00:00+00:00"
        valid, _, reason = supervisor.validate_operator_request(stale, now=time.time())
        self.assertFalse(valid)
        self.assertIn("stale", reason)

    def test_re_review_final_request_requires_unique_full_sha_targets(self) -> None:
        targets = [
            {"lane": "combat", "phase": "C17", "sha": "a" * 40},
            {"lane": "population", "phase": "L16", "sha": "b" * 40},
        ]
        request = operator_payload("final-recheck", "re-review-final", {"targets": targets})
        valid, _, reason = supervisor.validate_operator_request(request)
        self.assertTrue(valid, reason)

        targets[1]["sha"] = "b" * 39
        invalid = operator_payload("bad-final-recheck", "re-review-final", {"targets": targets})
        valid, _, reason = supervisor.validate_operator_request(invalid)
        self.assertFalse(valid)
        self.assertIn("full Git SHA", reason)

    def test_final_review_recheck_requires_paused_exact_blocked_targets(self) -> None:
        h = Harness()
        h.state["global_mode"] = "PAUSED"
        h.state["control_plane"] = {"status": "VALID"}
        h.state["scheduler"] = {"tasks": {}}
        review_state = h.state.setdefault("architect_review", {"items": {}, "queue": []})
        review_state.update({"items": {}, "queue": []})
        target_shas = {"combat": "a" * 40, "population": "b" * 40}
        phases = {"combat": "C17", "population": "L16"}
        for name, phase in phases.items():
            h.state["lanes"][name] = {
                "state": "BLOCKED", "phase_id": None, "phase_index": 1,
                "plan_data": {"phases": [{"id": phase, "title": "last phase"}], "boundaries": []},
                "worktree": name, "last_commit": target_shas[name], "worker_pid": None,
                "review": {
                    "type": "FINAL_MILESTONE_OR_QUEUE_REVIEW",
                    "reviewed_phase": {"id": phase}, "commit_sha": target_shas[name],
                    "decision": "SOL_BLOCKED", "review_tier": "architect",
                },
            }
            review_state["items"][f"blocked-{name}"] = {
                "lane": name, "phase": phase, "reviewed_sha": target_shas[name],
                "review_type": "FINAL_MILESTONE_OR_QUEUE_REVIEW", "evidence_version": 3,
                "status": "BLOCKED", "review_tier": "architect",
                "decision": {"decision": "RETRY"},
                "application_reason": (
                    "decision RETRY is not valid for review type FINAL_MILESTONE_OR_QUEUE_REVIEW"
                ),
            }
        h.git_head = lambda worktree: target_shas[str(worktree)]
        h._control_plane_valid = lambda: True
        h._recompute_scheduler = lambda: None
        h.review_process = None
        h.active_review_id = None

        def queue_exact_reviews():
            for target in review_state["operator_v3_targets"]:
                review_id = f"fresh-{target['lane']}"
                review_state["items"][review_id] = {
                    **target, "review_id": review_id,
                    "reviewed_sha": target["sha"],
                    "review_type": "FINAL_MILESTONE_OR_QUEUE_REVIEW",
                    "status": "QUEUED", "operator_authorized_recheck": True,
                }
                review_state["queue"].append(review_id)
            return len(review_state["operator_v3_targets"])

        h.queue_sol_reviews = queue_exact_reviews
        targets = [
            {"lane": name, "phase": phases[name], "sha": target_shas[name]}
            for name in phases
        ]
        self.assertEqual(h.request_final_reviews_recheck(targets), 0)
        self.assertTrue(review_state["read_only_while_paused"])
        self.assertEqual(len(review_state["operator_v3_targets"]), 2)
        self.assertEqual(review_state["queue"], ["fresh-combat", "fresh-population"])
        self.assertEqual(h.state["lanes"]["combat"]["state"], "NEEDS_SOL_REVIEW")
        self.assertEqual(h.state["lanes"]["population"]["state"], "NEEDS_SOL_REVIEW")

    def test_final_review_recheck_refuses_sha_drift_without_mutation(self) -> None:
        h = Harness()
        h.state["global_mode"] = "PAUSED"
        h.state["control_plane"] = {"status": "VALID"}
        h.state["architect_review"] = {"items": {}, "queue": []}
        h.state["lanes"]["combat"] = {
            "state": "BLOCKED", "phase_id": None, "phase_index": 1,
            "plan_data": {"phases": [{"id": "C17", "title": "last phase"}], "boundaries": []},
            "worktree": "combat", "last_commit": "a" * 40, "worker_pid": None,
            "review": {
                "type": "FINAL_MILESTONE_OR_QUEUE_REVIEW",
                "reviewed_phase": {"id": "C17"}, "commit_sha": "a" * 40,
                "decision": "SOL_BLOCKED",
            },
        }
        h.state["architect_review"]["items"]["blocked-combat"] = {
            "lane": "combat", "phase": "C17", "reviewed_sha": "a" * 40,
            "review_type": "FINAL_MILESTONE_OR_QUEUE_REVIEW", "evidence_version": 3,
            "review_tier": "architect",
            "status": "BLOCKED", "decision": {"decision": "RETRY"},
            "application_reason": "decision RETRY is not valid for review type FINAL_MILESTONE_OR_QUEUE_REVIEW",
        }
        h.git_head = lambda _worktree: "c" * 40
        h._control_plane_valid = lambda: True
        targets = [{"lane": "combat", "phase": "C17", "sha": "a" * 40}]

        self.assertEqual(h.request_final_reviews_recheck(targets), 1)
        self.assertEqual(h.state["lanes"]["combat"]["state"], "BLOCKED")
        self.assertFalse(h.state["architect_review"].get("read_only_while_paused", False))

    def test_final_review_packet_explains_retry_and_runtime_boundary(self) -> None:
        h = Harness()
        h.state["control_plane"] = {"applied_sha": "c" * 40}
        h.state["global_mode"] = "PAUSED"
        h.state["lanes"]["combat"] = {
            "branch": "parallel/combat-foundations", "worktree": "/lane/combat", "issue": 31,
            "phase_id": None, "history": [], "validation": {}, "ci": {},
            "plan_data": {"phases": [], "boundaries": []},
            "review": {"type": "FINAL_MILESTONE_OR_QUEUE_REVIEW", "reviewed_phase": {"id": "C17"},
                       "commit_sha": "a" * 40, "next_phase": None},
        }
        with patch.object(supervisor, "REVIEW_ROOT", Path("review-output")), \
                patch.object(Path, "write_text", autospec=True) as write_text, \
                patch.object(supervisor.os, "chmod"):
            packet = h.make_review_packet("combat", ["final queue review"])
        self.assertEqual(packet.name, "combat-FINAL.md")
        text = write_text.call_args.args[1]
        self.assertIn("FINAL_MILESTONE_OR_QUEUE_REVIEW", text)
        self.assertIn("APPROVE (close the empty engineering queue)", text)
        self.assertIn("RETRY (reopen the last completed phase for bounded repair)", text)
        self.assertIn("Runtime and milestone acceptance remain human decisions", text)

    def test_daemon_owned_approve_applies_to_current_memory_and_emits_receipt(self) -> None:
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
                "next_phase": {"id": "C04", "title": "next"},
            },
        }
        h._refresh_plan = lambda _lane, lane: lane.update(
            {"phase_id": "C04", "phase_title": "next"}
        )
        put_operator_request(
            h, operator_payload("approve-current", "approve", {"lane": "combat"})
        )
        self.assertEqual(h.process_operator_requests(), 1)
        self.assertEqual(h.state["lanes"]["combat"]["phase_id"], "C04")
        receipt = supervisor.read_json(
            supervisor.operator_request_paths(h.config)["receipts"] / "approve-current.json", {}
        )
        self.assertEqual(receipt["status"], "SUCCEEDED")
        self.assertEqual(receipt["daemon_pid"], os.getpid())

    def test_daemon_save_after_operator_request_cannot_revert_live_mutation(self) -> None:
        h = Harness()
        h.state["lanes"]["combat"] = {"state": "READY", "review": {}}
        persisted: list[dict] = []

        def save() -> bool:
            if h._deferred_save_depth:
                h._save_deferred = True
                return True
            persisted.append(deepcopy(h.state))
            return True

        h.save_state = save  # type: ignore[method-assign]
        put_operator_request(
            h, operator_payload("block-current", "block", {"lane": "combat"})
        )
        self.assertEqual(h.process_operator_requests(), 1)
        self.assertEqual(len(persisted), 1)
        self.assertEqual(persisted[0]["lanes"]["combat"]["state"], "BLOCKED")
        self.assertIn("block-current", persisted[0]["processed_operator_requests"])

    def test_failed_request_save_rolls_back_memory_and_retries_same_daemon(self) -> None:
        h = DurableSaveHarness()
        h.state["lanes"]["combat"] = {"state": "READY", "review": {}}
        h.persisted_state = deepcopy(h.state)
        payload = operator_payload("save-failure-same-daemon", "block", {"lane": "combat"})
        path = put_operator_request(h, payload)
        paths = supervisor.operator_request_paths(h.config)
        dispatches: list[str] = []
        original_dispatch = h._dispatch_operator_request

        def counted_dispatch(request: dict) -> tuple[int, str, str]:
            dispatches.append(str(request["request_id"]))
            return original_dispatch(request)

        h._dispatch_operator_request = counted_dispatch  # type: ignore[method-assign]
        h.fail_next_save = True
        self.assertEqual(h.process_operator_requests(), 0)
        self.assertEqual(dispatches, ["save-failure-same-daemon"])
        self.assertEqual(h.state["lanes"]["combat"]["state"], "READY")
        self.assertNotIn("save-failure-same-daemon", h.state["processed_operator_requests"])
        self.assertTrue(path.exists())
        self.assertFalse((paths["receipts"] / "save-failure-same-daemon.json").exists())

        self.assertEqual(h.process_operator_requests(), 1)
        self.assertEqual(dispatches, ["save-failure-same-daemon"] * 2)
        self.assertEqual(h.state["lanes"]["combat"]["state"], "BLOCKED")
        self.assertIn("save-failure-same-daemon", h.persisted_state["processed_operator_requests"])
        self.assertFalse(path.exists())
        self.assertEqual(
            supervisor.read_json(paths["receipts"] / "save-failure-same-daemon.json", {})[
                "status"
            ],
            "SUCCEEDED",
        )

    def test_failed_request_save_restart_replays_from_last_persisted_state(self) -> None:
        h = DurableSaveHarness()
        h.state["lanes"]["combat"] = {"state": "READY", "review": {}}
        h.persisted_state = deepcopy(h.state)
        payload = operator_payload("save-failure-restart", "block", {"lane": "combat"})
        path = put_operator_request(h, payload)
        h.fail_next_save = True
        self.assertEqual(h.process_operator_requests(), 0)
        self.assertEqual(h.state, h.persisted_state)
        self.assertTrue(path.exists())

        restarted = DurableSaveHarness()
        restarted.config["operator_request_root"] = h.config["operator_request_root"]
        restarted.state = deepcopy(h.persisted_state)
        restarted.persisted_state = deepcopy(h.persisted_state)
        dispatches: list[str] = []
        original_dispatch = restarted._dispatch_operator_request

        def counted_dispatch(request: dict) -> tuple[int, str, str]:
            dispatches.append(str(request["request_id"]))
            return original_dispatch(request)

        restarted._dispatch_operator_request = counted_dispatch  # type: ignore[method-assign]
        self.assertEqual(restarted.process_operator_requests(), 1)
        self.assertEqual(dispatches, ["save-failure-restart"])
        self.assertEqual(restarted.state["lanes"]["combat"]["state"], "BLOCKED")
        self.assertIn("save-failure-restart", restarted.persisted_state["processed_operator_requests"])
        self.assertFalse(path.exists())

    def test_failed_malformed_request_save_does_not_leave_marker(self) -> None:
        h = DurableSaveHarness()
        payload = operator_payload("malformed-save-failure", "not-supported", {})
        path = put_operator_request(h, payload)
        h.fail_next_save = True
        self.assertEqual(h.process_operator_requests(), 0)
        self.assertNotIn("malformed-save-failure", h.state["processed_operator_requests"])
        self.assertTrue(path.exists())
        self.assertFalse(
            (supervisor.operator_request_paths(h.config)["receipts"]
             / "malformed-save-failure.json").exists()
        )

    def test_successful_request_duplicate_and_restart_dispatch_only_once(self) -> None:
        h = DurableSaveHarness()
        h.state["lanes"]["combat"] = {"state": "READY", "review": {}}
        h.persisted_state = deepcopy(h.state)
        payload = operator_payload("durable-duplicate", "block", {"lane": "combat"})
        put_operator_request(h, payload)
        dispatches: list[str] = []
        original_dispatch = h._dispatch_operator_request

        def counted_dispatch(request: dict) -> tuple[int, str, str]:
            dispatches.append(str(request["request_id"]))
            return original_dispatch(request)

        h._dispatch_operator_request = counted_dispatch  # type: ignore[method-assign]
        self.assertEqual(h.process_operator_requests(), 1)
        put_operator_request(h, payload)
        self.assertEqual(h.process_operator_requests(), 1)
        self.assertEqual(dispatches, ["durable-duplicate"])

        restarted = DurableSaveHarness()
        restarted.config["operator_request_root"] = h.config["operator_request_root"]
        restarted.state = deepcopy(h.persisted_state)
        restarted.persisted_state = deepcopy(h.persisted_state)
        restart_dispatches: list[str] = []
        original_restart_dispatch = restarted._dispatch_operator_request

        def counted_restart_dispatch(request: dict) -> tuple[int, str, str]:
            restart_dispatches.append(str(request["request_id"]))
            return original_restart_dispatch(request)

        restarted._dispatch_operator_request = counted_restart_dispatch  # type: ignore[method-assign]
        put_operator_request(restarted, payload)
        self.assertEqual(restarted.process_operator_requests(), 1)
        self.assertEqual(restart_dispatches, [])
        self.assertEqual(restarted.state["lanes"]["combat"]["state"], "BLOCKED")

    def test_sync_control_plane_precommit_replay_is_idempotent(self) -> None:
        h = DurableSaveHarness()
        h.state["control_plane"] = {"applied_sha": "old"}
        h.roadmap_snapshot = SimpleNamespace(value="old")
        h.persisted_state = deepcopy(h.state)
        external = {"head": "old", "fetches": 0, "merges": 0}

        def idempotent_sync(force: bool = False) -> bool:
            external["fetches"] += 1
            if external["head"] == "old":
                external["head"] = "new"
                external["merges"] += 1
            h.state["control_plane"]["applied_sha"] = external["head"]
            h.roadmap_snapshot = SimpleNamespace(value=external["head"])
            return True

        h.sync_control_plane = idempotent_sync  # type: ignore[method-assign]
        payload = operator_payload("idempotent-sync", "sync-control-plane", {"force": True})
        put_operator_request(h, payload)
        h.fail_next_save = True
        self.assertEqual(h.process_operator_requests(), 0)
        self.assertEqual(h.state["control_plane"]["applied_sha"], "old")
        self.assertEqual(h.roadmap_snapshot.value, "old")
        self.assertEqual(external, {"head": "new", "fetches": 1, "merges": 1})

        self.assertEqual(h.process_operator_requests(), 1)
        self.assertEqual(h.state["control_plane"]["applied_sha"], "new")
        self.assertEqual(h.roadmap_snapshot.value, "new")
        self.assertEqual(external, {"head": "new", "fetches": 2, "merges": 1})

    def test_duplicate_operator_request_is_not_executed_after_restart(self) -> None:
        h = Harness()
        h.state["lanes"]["combat"] = {"state": "READY", "review": {}}
        payload = operator_payload("duplicate-block", "block", {"lane": "combat"})
        put_operator_request(h, payload)
        self.assertEqual(h.process_operator_requests(), 1)
        event_count = len(h.state["events"])
        put_operator_request(h, payload)
        self.assertEqual(h.process_operator_requests(), 1)
        self.assertEqual(len(h.state["events"]), event_count)

        restarted = Harness()
        restarted.config["operator_request_root"] = h.config["operator_request_root"]
        restarted.state["lanes"]["combat"] = {"state": "READY", "review": {}}
        restarted.state["processed_operator_requests"] = deepcopy(
            h.state["processed_operator_requests"]
        )
        put_operator_request(restarted, payload)
        self.assertEqual(restarted.process_operator_requests(), 1)
        self.assertEqual(restarted.state["lanes"]["combat"]["state"], "READY")

    def test_processed_operator_request_history_is_bounded(self) -> None:
        h = Harness()
        h.state["lanes"]["combat"] = {"state": "READY", "review": {}}
        for index in range(20):
            put_operator_request(
                h,
                operator_payload(
                    f"bounded-{index}", "block", {"lane": "combat"}
                ),
            )
            self.assertEqual(h.process_operator_requests(), 1)
        self.assertLessEqual(
            len(h.state["processed_operator_requests"]),
            16,
        )

    def test_malformed_operator_request_fails_closed(self) -> None:
        h = Harness()
        paths = supervisor.ensure_operator_request_dirs(h.config)
        supervisor.atomic_write_text(paths["inbox"] / "malformed.json", "{not-json", 0o600)
        self.assertEqual(h.process_operator_requests(), 1)
        receipt = supervisor.read_json(paths["receipts"] / "malformed.json", {})
        self.assertEqual(receipt["status"], "FAILED")
        self.assertEqual(h.state["lanes"], {})

    def test_operator_mutation_dispatch_covers_task_control_milestone_and_sync(self) -> None:
        h = Harness()
        cases = [
            ("retry", {"lane": "combat"}, "retry_review"),
            ("block", {"lane": "combat"}, "block_lane"),
            ("approve-task", {"task_id": "W01"}, "approve_task"),
            ("approve-control-plane", {"sha": "a" * 40}, "approve_control_plane"),
            ("accept-milestone", {"milestone_id": "M01", "evidence": {}}, "accept_milestone"),
            ("sync-control-plane", {"force": True}, "sync_control_plane"),
            ("re-review-final", {"targets": [{"lane": "combat", "phase": "C17", "sha": "a" * 40}]}, "request_final_reviews_recheck"),
        ]
        for command, args, method in cases:
            return_value = True if command == "sync-control-plane" else 0
            with self.subTest(command=command), patch.object(h, method, return_value=return_value) as called:
                code, _out, _err = h._dispatch_operator_request(
                    operator_payload("dispatch-" + command, command, args)
                )
                self.assertEqual(code, 0)
                called.assert_called_once()

    def test_daemon_inactive_operator_mutation_does_not_create_request(self) -> None:
        h = Harness()
        paths = supervisor.operator_request_paths(h.config)
        with patch.object(supervisor, "daemon_is_active", return_value=(False, "inactive")):
            self.assertEqual(
                supervisor.submit_operator_request(
                    "block", {"lane": "combat"}, request_id="offline-block", config=h.config
                ),
                1,
            )
        self.assertFalse(paths["inbox"].exists())

    def test_cli_mutation_never_constructs_observer_state_writer(self) -> None:
        with patch.object(supervisor, "submit_operator_request", return_value=0) as submit, \
                patch.object(supervisor, "Supervisor") as constructor, \
                patch.object(sys, "argv", ["supervisor.py", "block", "combat"]):
            self.assertEqual(supervisor.main(), 0)
        constructor.assert_not_called()
        submit.assert_called_once_with(
            "block", {"lane": "combat"}, config=supervisor.read_json(supervisor.CONFIG_PATH, {})
        )

    def test_cli_parses_exact_final_review_recheck_targets(self) -> None:
        sha = "a" * 40
        with patch.object(supervisor, "submit_operator_request", return_value=0) as submit, \
                patch.object(supervisor, "Supervisor") as constructor, \
                patch.object(sys, "argv", ["supervisor.py", "re-review-final", "--target", "combat", "C17", sha]):
            self.assertEqual(supervisor.main(), 0)
        constructor.assert_not_called()
        submit.assert_called_once_with(
            "re-review-final",
            {"targets": [{"lane": "combat", "phase": "C17", "sha": sha}]},
            config=supervisor.read_json(supervisor.CONFIG_PATH, {}),
        )

    def test_complete_with_validation_gap_enters_local_validation_and_persists_reason(self) -> None:
        h = Harness()
        root = make_git_repo()
        lane = configure_combat_lane(h, root, "CODING")
        log = root / "worker.log"
        log.write_text(
            "implementation complete\nWORKER_RESULT: COMPLETE_WITH_VALIDATION_GAP\n"
            "VALIDATION_GAP: xmake executable unavailable; TPTests not run locally\n",
            encoding="utf-8",
        )
        lane["worker_log"] = str(log)
        process = SimpleNamespace(returncode=0, poll=lambda: 0)
        h.processes["combat"] = process
        h.poll_workers()
        self.assertEqual(lane["state"], "LOCAL_VALIDATION")
        self.assertEqual(lane["worker_result"], "COMPLETE_WITH_VALIDATION_GAP")
        self.assertIn("xmake executable unavailable", lane["validation_gap"]["reason"])

    def test_validation_gap_without_reason_is_not_accepted(self) -> None:
        h = Harness()
        root = make_git_repo()
        lane = configure_combat_lane(h, root, "CODING")
        log = root / "worker.log"
        log.write_text("WORKER_RESULT: COMPLETE_WITH_VALIDATION_GAP\n", encoding="utf-8")
        lane["worker_log"] = str(log)
        h.processes["combat"] = SimpleNamespace(returncode=0, poll=lambda: 0)
        h.poll_workers()
        self.assertEqual(lane["state"], "RECOVERING")
        self.assertIn("without a bounded", lane["last_error"])

    def test_real_failed_check_is_not_converted_to_validation_gap(self) -> None:
        h = Harness()
        root = make_git_repo()
        lane = configure_combat_lane(h, root)
        (root / "bad.txt").write_text("trailing-space \n", encoding="utf-8")
        lane["worker_result"] = "COMPLETE_WITH_VALIDATION_GAP"
        lane["validation_gap"] = {"status": "PRESENT", "reason": "tool absent", "at": supervisor.utc_now()}
        self.assertFalse(h.local_validate("combat"))
        self.assertEqual(lane["validation"]["status"], "FAIL")
        self.assertEqual(lane["validation"]["validation_gap"]["status"], "INVALIDATED")

    def test_review_lane_consumes_no_slot_while_two_ready_lanes_run(self) -> None:
        h = SchedulingHarness(
            {"combat": "NEEDS_SOL_REVIEW", "authority": "READY", "population": "READY", "ui": "READY"},
            {"authority": "READY", "population": "READY", "ui": "READY"},
        )
        h.schedule()
        self.assertEqual(set(h.processes), {"authority", "population"})
        self.assertNotIn("combat", h.processes)

    def test_waiting_checkpoint_does_not_block_unrelated_ready_lanes(self) -> None:
        h = SchedulingHarness(
            {"combat": "NEEDS_SOL_REVIEW", "authority": "WAITING_FOR_CI", "population": "READY", "ui": "READY"},
            {"population": "READY", "ui": "READY"},
        )
        h.schedule()
        self.assertEqual(set(h.processes), {"population", "ui"})
        self.assertNotIn("authority", h.processes)

    def test_resume_with_three_recoveries_uses_capped_real_run_once_path(self) -> None:
        h = ProductionPathHarness()

        def spawn(*_args, **_kwargs):
            return FakeWorkerProcess()

        with patch.object(
            supervisor,
            "read_json",
            return_value={"desired_mode": "RUNNING", "reason": "operator resumed"},
        ), patch.object(supervisor.subprocess, "Popen", side_effect=spawn) as worker_spawn:
            h.run_once()

        self.assertEqual(h.state["global_mode"], "RUNNING")
        self.assertEqual(len(h.processes), 2)
        self.assertLessEqual(len(h.processes), h.config["max_concurrent_workers"])
        self.assertEqual(len(h.start_calls), 2)
        self.assertTrue(any(recovery for _lane_name, recovery in h.start_calls))
        commands = [call.args[0] for call in worker_spawn.call_args_list]
        self.assertEqual(len(commands), 2)
        for command in commands:
            self.assertEqual(command[command.index("--model") + 1], "gpt-6-luna")
            self.assertEqual(command[command.index("--config") + 1], 'model_reasoning_effort="max"')

    def test_advance_lane_cannot_admit_recovery_workers_outside_schedule(self) -> None:
        h = SchedulingHarness(
            {lane_name: "RECOVERING" for lane_name in supervisor.LANE_ORDER},
            {lane_name: "READY" for lane_name in supervisor.LANE_ORDER},
        )
        with patch.object(
            supervisor,
            "read_json",
            return_value={"desired_mode": "RUNNING", "reason": "operator resumed"},
        ):
            for lane_name in ("combat", "population", "ui"):
                h.advance_lane(lane_name)

        self.assertEqual(h.start_calls, [])
        self.assertEqual(h.processes, {})

    def test_start_worker_defense_in_depth_rejects_third_live_process(self) -> None:
        h = ProductionPathHarness()
        h.state["global_mode"] = "RUNNING"
        h.state["lanes"]["ui"]["state"] = "READY"
        h.processes = {
            "combat": SimpleNamespace(pid=1),
            "authority": SimpleNamespace(pid=2),
        }
        with patch.object(supervisor.subprocess, "Popen") as spawn:
            self.assertFalse(h.start_worker("ui"))

        spawn.assert_not_called()
        self.assertEqual(set(h.processes), {"combat", "authority"})

    def test_scheduler_refills_one_finished_slot_without_exceeding_cap(self) -> None:
        h = SchedulingHarness(
            {lane_name: "READY" for lane_name in supervisor.LANE_ORDER},
            {lane_name: "READY" for lane_name in supervisor.LANE_ORDER},
        )
        h.schedule()
        self.assertEqual(len(h.processes), 2)
        initial = set(h.processes)
        finished = next(iter(initial))
        h.processes.pop(finished)
        calls_before_refill = len(h.start_calls)

        h.schedule()

        self.assertEqual(len(h.processes), 2)
        self.assertLessEqual(len(h.processes), h.config["max_concurrent_workers"])
        self.assertEqual(len(h.start_calls) - calls_before_refill, 1)
        self.assertEqual(len(set(h.processes) - (initial - {finished})), 1)

    def test_review_gated_lane_consumes_no_slot_while_recovery_fills_one(self) -> None:
        h = SchedulingHarness(
            {"combat": "NEEDS_SOL_REVIEW", "authority": "READY", "population": "RECOVERING", "ui": "READY"},
            {"authority": "READY", "population": "READY", "ui": "READY"},
        )

        h.schedule()

        self.assertNotIn("combat", h.processes)
        self.assertEqual(len(h.processes), 2)
        self.assertIn("population", h.processes)
        self.assertIn(("population", True), h.start_calls)

    def test_non_worker_lane_states_do_not_consume_worker_slots(self) -> None:
        for non_worker_state in (
            "WAITING_FOR_CI",
            "LOCAL_VALIDATION",
            "COMMITTING",
            "PUSHING",
            "NEEDS_SOL_REVIEW",
        ):
            h = SchedulingHarness(
                {"combat": non_worker_state, "authority": "NEEDS_SOL_REVIEW", "population": "NEEDS_SOL_REVIEW", "ui": "NEEDS_SOL_REVIEW"},
                {},
            )
            h.state["scheduler"]["tasks"]["C01"]["state"] = "READY"

            h.schedule()

            self.assertEqual(h.processes, {}, non_worker_state)

    def test_rate_limit_probe_cannot_bypass_global_worker_cap(self) -> None:
        h = SchedulingHarness(
            {lane_name: "READY" for lane_name in supervisor.LANE_ORDER},
            {lane_name: "READY" for lane_name in supervisor.LANE_ORDER},
        )
        h.state["codex_availability"] = {
            "status": "RATE_LIMITED",
            "next_retry_at": "1970-01-01T00:00:00+00:00",
            "backoff_seconds": 900,
            "retry_count": 1,
            "probe_started_at": None,
        }
        h.processes = {
            "combat": SimpleNamespace(pid=1),
            "authority": SimpleNamespace(pid=2),
        }

        h.schedule()

        self.assertEqual(h.start_calls, [])
        self.assertEqual(len(h.processes), 2)

    def test_failed_spawn_leaves_slot_for_another_eligible_lane(self) -> None:
        h = ProductionPathHarness()
        h.state["global_mode"] = "RUNNING"
        for lane in h.state["lanes"].values():
            lane["state"] = "READY"
            lane.pop("paused_from_state", None)
        spawn_count = 0

        def spawn(*_args, **_kwargs):
            nonlocal spawn_count
            spawn_count += 1
            if spawn_count == 1:
                raise OSError("simulated spawn failure")
            return FakeWorkerProcess()

        with patch.object(supervisor.subprocess, "Popen", side_effect=spawn):
            h.schedule()

        failed_lane = h.start_calls[0][0]
        self.assertEqual(h.state["lanes"][failed_lane]["state"], "RECOVERING")
        self.assertNotIn(failed_lane, h.processes)
        self.assertGreaterEqual(len(h.start_calls), 2)
        self.assertTrue(any(lane != failed_lane for lane, _recovery in h.start_calls[1:]))
        self.assertEqual(len(h.processes), 2)
        self.assertLessEqual(len(h.processes), h.config["max_concurrent_workers"])

    def test_all_review_gated_lanes_remain_healthy_and_idle(self) -> None:
        h = SchedulingHarness(
            {lane: "NEEDS_SOL_REVIEW" for lane in supervisor.LANE_ORDER},
            {},
        )
        h.schedule()
        summary = h.scheduler_idle_summary()
        self.assertEqual(h.processes, {})
        self.assertEqual(summary["runnable"], [])
        self.assertEqual(summary["review_gated"], sorted(supervisor.LANE_ORDER))

    def test_external_future_tasks_are_not_speculatively_started(self) -> None:
        h = SchedulingHarness(
            {lane: "NEEDS_SOL_REVIEW" for lane in supervisor.LANE_ORDER},
            {},
        )
        for index in range(1, 11):
            task_id = f"W{index:02d}"
            h.state["scheduler"]["tasks"][task_id] = {
                "task_id": task_id,
                "source": "ROADMAP",
                "state": "BLOCKED_EXTERNAL_GATE",
                "lane": "combat",
            }
        h.schedule()
        summary = h.scheduler_idle_summary()
        self.assertEqual(h.processes, {})
        self.assertEqual(summary["external_gated_future_tasks"], 10)
        self.assertTrue(all(
            h.state["scheduler"]["tasks"][f"W{index:02d}"]["state"] == "BLOCKED_EXTERNAL_GATE"
            for index in range(1, 11)
        ))

    def test_status_idle_summary_explains_review_checkpoint_and_external_gate(self) -> None:
        h = SchedulingHarness(
            {"combat": "NEEDS_SOL_REVIEW", "authority": "NEEDS_SOL_REVIEW", "population": "READY", "ui": "READY"},
            {},
        )
        h.state["lanes"]["authority"]["review"] = {"type": "POST_PHASE_CHECKPOINT"}
        h.state["scheduler"]["tasks"]["W01"] = {
            "task_id": "W01", "source": "ROADMAP", "state": "BLOCKED_EXTERNAL_GATE"
        }
        summary = h.scheduler_idle_summary()
        self.assertEqual(summary["runnable"], [])
        self.assertIn("authority", summary["waiting_checkpoint"])
        self.assertEqual(summary["external_gated_future_tasks"], 1)

    def test_terminal_architect_block_is_persisted_as_blocked_review_not_ready(self) -> None:
        h = TerminalReviewSchedulerHarness()
        h._recompute_scheduler()
        record = h.state["scheduler"]["tasks"]["C01"]
        self.assertEqual(record["state"], "BLOCKED_REVIEW")
        self.assertIn("terminal architect BLOCK", record["reason"])
        self.assertNotEqual(record["state"], "READY")

    def test_blocked_active_lane_and_external_future_gate_are_explained(self) -> None:
        h = SchedulingHarness(
            {lane: "NEEDS_SOL_REVIEW" for lane in supervisor.LANE_ORDER}, {},
        )
        lane = h.state["lanes"]["combat"]
        lane.update({"state": "BLOCKED", "last_commit": "a" * 40,
                     "review": {"type": "POST_PHASE_CHECKPOINT", "decision": "SOL_BLOCKED",
                                "commit_sha": "a" * 40}})
        h.state["scheduler"]["tasks"]["future"] = {
            "task_id": "future", "source": "ROADMAP", "state": "BLOCKED_EXTERNAL_GATE"
        }
        summary = h.scheduler_idle_summary()
        self.assertIn("combat", summary["review_gated"])
        self.assertIn("combat", summary["waiting_checkpoint"])
        self.assertIn("active lanes blocked", summary["idle_reason"])
        self.assertEqual(summary["external_gated_future_tasks"], 1)

    def test_review_evidence_error_is_separate_from_lane_decision(self) -> None:
        h = Harness()
        h.config["architect_review"] = {"deterministic_evidence_error_max_attempts": 3, "evidence_error_backoff_seconds": 1}
        h.state["lanes"]["combat"] = {"state": "NEEDS_SOL_REVIEW", "phase_id": "C05"}
        h.state["architect_review"] = {"items": {}}
        before = deepcopy(h.state["architect_review"]["items"])
        error = supervisor.ReviewEvidenceError("EXACT_SHA_CI_MISSING", "CI evidence unavailable", deterministic=True)
        item = h._record_review_evidence_error("combat", "C05", "POST_PHASE_CHECKPOINT", "a" * 40, error)
        self.assertEqual(item["status"], "EVIDENCE_ERROR")
        self.assertEqual(h.state["lanes"]["combat"]["state"], "NEEDS_SOL_REVIEW")
        self.assertEqual(h.state["architect_review"]["items"], before)
        self.assertEqual(len(h.state["architect_review"]["evidence_errors"]), 1)

    def test_deterministic_evidence_error_escalates_after_bounded_attempts(self) -> None:
        h = Harness()
        h.config["architect_review"] = {"deterministic_evidence_error_max_attempts": 2, "evidence_error_backoff_seconds": 1}
        h.state["architect_review"] = {"items": {}}
        error = supervisor.ReviewEvidenceError("TRUSTED_BASE_MISSING", "no accepted base", deterministic=True)
        first = deepcopy(h._record_review_evidence_error("combat", "C05", "CURRENT_PHASE_REVIEW", "b" * 40, error))
        second = h._record_review_evidence_error("combat", "C05", "CURRENT_PHASE_REVIEW", "b" * 40, error)
        self.assertEqual(first["status"], "EVIDENCE_ERROR")
        self.assertEqual(first["attempts"], 1)
        self.assertEqual(second["status"], "REQUIRES_INFRA_REVIEW")
        self.assertEqual(second["attempts"], 2)
        self.assertIsNone(second["next_retry_at"])

    def test_required_workflows_all_success(self) -> None:
        runs = [
            {"workflowName": "Build windows", "headSha": "abc", "status": "completed", "conclusion": "success", "databaseId": 2},
            {"workflowName": "Build linux", "headSha": "abc", "status": "completed", "conclusion": "success", "databaseId": 1},
        ]
        result = supervisor.aggregate_required_workflows(runs, ["Build linux", "Build windows"], "abc")
        self.assertEqual(result["status"], "PASS")

    def test_pending_review_refreshes_exact_sha_ci_and_reopens_missing_evidence(self) -> None:
        h = Harness()
        h.runtime_owner = False
        h._control_plane_valid = lambda: True
        head = "a" * 40
        phase = "A12"
        h.state["control_plane"] = {"applied_sha": "control-plane-sha"}
        h.state["lanes"]["combat"] = {
            "state": "NEEDS_SOL_REVIEW", "phase_id": phase, "last_commit": head,
            "worker_attempt": 0, "ci": {"status": "NOT_RUN", "sha": head},
            "review": {
                "type": "CURRENT_PHASE_REVIEW", "commit_sha": head,
                "reviewed_phase": {"id": phase}, "review_tier": "normal",
                "selected_model": "gpt-6-luna", "selected_reasoning_effort": "max",
            },
        }
        evidence_error = {
            "lane": "combat", "phase": phase, "reviewed_sha": head,
            "review_type": "CURRENT_PHASE_REVIEW", "status": "REQUIRES_INFRA_REVIEW",
            "error_code": "EXACT_SHA_CI_WORKFLOW_MISSING", "attempts": 3,
        }
        h.state["architect_review"] = {
            "enabled": True, "active_review_id": None, "queue": [], "items": {},
            "evidence_errors": {"ci-error": evidence_error}, "bundle_failures": {},
        }
        h.ci_run_list = lambda sha: (0, [
            {"workflowName": "Build windows", "headSha": sha, "status": "completed", "conclusion": "success", "databaseId": 2},
            {"workflowName": "Build linux", "headSha": sha, "status": "completed", "conclusion": "success", "databaseId": 1},
            {"workflowName": "Build linux", "headSha": "b" * 40, "status": "completed", "conclusion": "success", "databaseId": 3},
        ], "")
        review_bundle = {
            "review_id": "exact-sha-review", "review_state_sha256": "c" * 64,
            "evidence_version": 3, "bundle_schema_version": 3,
            "phase": phase, "reviewed_sha": head, "review_type": "CURRENT_PHASE_REVIEW",
            "worker_attempt": 0, "bundle_dir": "/tmp/bundles/exact-sha-review",
            "bundle_path": "/tmp/bundles/exact-sha-review/bundle.json",
        }
        h.build_review_bundle = lambda _lane: review_bundle

        self.assertEqual(h.queue_sol_reviews(), 1)
        self.assertEqual(h.state["lanes"]["combat"]["ci"]["status"], "PASS")
        self.assertEqual(
            [row["workflow"] for row in h.state["lanes"]["combat"]["ci"]["required_workflows"]],
            ["Build linux", "Build windows"],
        )
        self.assertEqual(evidence_error["status"], "RESOLVED")
        self.assertEqual(h.state["architect_review"]["queue"], ["exact-sha-review"])

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

    def test_prospective_diff_accepts_tracked_edit_without_real_index_mutation(self) -> None:
        root = make_git_repo()
        (root / "tracked.txt").write_text("valid edit\n", encoding="utf-8")
        h = Harness()
        before = subprocess.check_output(["git", "-C", str(root), "ls-files", "--stage", "-z"])
        result = h.prospective_commit_diff_check(str(root), ["tracked.txt"])
        after = subprocess.check_output(["git", "-C", str(root), "ls-files", "--stage", "-z"])
        self.assertEqual(result["status"], "PASS")
        self.assertTrue(result["real_index_unchanged"])
        self.assertEqual(before, after)

    def test_prospective_diff_accepts_new_untracked_text_file(self) -> None:
        root = make_git_repo()
        (root / "new.txt").write_text("valid new file\n", encoding="utf-8")
        h = Harness()
        result = h.prospective_commit_diff_check(str(root), ["new.txt"])
        self.assertEqual(result["status"], "PASS")
        self.assertTrue(result["real_index_unchanged"])

    def test_untracked_whitespace_fails_before_real_index_mutation(self) -> None:
        root = make_git_repo()
        (root / "bad.txt").write_text("line with trailing space \n", encoding="utf-8")
        h = Harness()
        before = subprocess.check_output(["git", "-C", str(root), "ls-files", "--stage", "-z"])
        result = h.prospective_commit_diff_check(str(root), ["bad.txt"])
        after = subprocess.check_output(["git", "-C", str(root), "ls-files", "--stage", "-z"])
        self.assertEqual(result["status"], "FAIL")
        self.assertFalse(result["diff_check"])
        self.assertTrue(result["real_index_unchanged"])
        self.assertEqual(before, after)

    def test_prospective_diff_represents_deletion_and_recovery_index_safely(self) -> None:
        root = make_git_repo()
        (root / "tracked.txt").unlink()
        h = Harness()
        result = h.prospective_commit_diff_check(str(root), ["tracked.txt"])
        self.assertEqual(result["status"], "PASS")
        self.assertTrue(result["real_index_unchanged"])

        recovery_root = make_git_repo()
        (recovery_root / "tracked.txt").write_text("staged version\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(recovery_root), "add", "tracked.txt"], check=True)
        (recovery_root / "tracked.txt").write_text("unstaged recovery version\n", encoding="utf-8")
        before = subprocess.check_output(["git", "-C", str(recovery_root), "ls-files", "--stage", "-z"])
        result = h.prospective_commit_diff_check(str(recovery_root), ["tracked.txt"])
        after = subprocess.check_output(["git", "-C", str(recovery_root), "ls-files", "--stage", "-z"])
        self.assertEqual(result["status"], "PASS")
        self.assertTrue(result["real_index_unchanged"])
        self.assertEqual(before, after)

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
        with patch.object(supervisor, "read_json", return_value={"desired_mode": "PAUSED"}):
            h.advance_lane("combat")
        self.assertEqual(called, [])
        self.assertEqual(h.state["lanes"]["combat"]["state"], "PAUSED")

    def test_active_worker_clears_stale_pause_idle_reason(self) -> None:
        h = Harness()
        h.state["control_plane"] = {"status": "VALID", "applied_sha": "a" * 40}
        h.state["architect_review"] = {
            "enabled": True, "active_review_id": None, "idle_reason": "global mode is paused",
            "items": {}, "queue": [], "phase_counters": {},
        }
        h.processes["combat"] = object()
        h.review_process = None
        h.active_review_id = None
        with patch.object(h, "_control_plane_valid", return_value=True):
            with patch.object(h, "queue_sol_reviews", return_value=0):
                with patch.object(h, "start_next_reviewer"):
                    h.architect_review_tick()
        self.assertIsNone(h.state["architect_review"]["idle_reason"])

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

    def _assert_observer_preserves_ownership(self, method_name: str) -> None:
        h = LiveWorkerObserverHarness()
        before = ownership_snapshot(h)
        result = getattr(h, method_name)()
        self.assertIn(result, (0, 1))
        self.assertEqual(ownership_snapshot(h), before)

    def test_status_observer_preserves_live_worker_ownership(self) -> None:
        self._assert_observer_preserves_ownership("status")

    def test_roadmap_status_observer_preserves_live_worker_ownership(self) -> None:
        self._assert_observer_preserves_ownership("roadmap_status")

    def test_milestone_status_observer_preserves_live_worker_ownership(self) -> None:
        self._assert_observer_preserves_ownership("milestone_status")

    def test_review_status_observer_preserves_live_worker_ownership(self) -> None:
        self._assert_observer_preserves_ownership("review_status")

    def test_healthcheck_observer_preserves_live_worker_ownership(self) -> None:
        self._assert_observer_preserves_ownership("healthcheck")

    def test_self_test_observer_preserves_live_worker_ownership(self) -> None:
        h = LiveWorkerObserverHarness()
        before = ownership_snapshot(h)
        with tempfile.TemporaryDirectory(prefix="skyrim-self-test-state-") as state_root, \
                tempfile.TemporaryDirectory(prefix="skyrim-product-") as product_root:
            product_path = Path(product_root)
            for name in ("PRODUCT_VISION.md", "WORLD_RULES.md", "MILESTONE_01_CORE_WORLD.md"):
                (product_path / name).write_text("test product context\n", encoding="utf-8")
            h.config["product_root"] = product_root
            with patch.object(supervisor, "STATE_DIR", Path(state_root)):
                result = h.self_test()
        self.assertEqual(result, 0)
        self.assertEqual(ownership_snapshot(h), before)

    def test_self_test_accepts_only_expected_conflict_free_review_diffs(self) -> None:
        h = LiveWorkerObserverHarness()
        for lane_name in supervisor.LANE_ORDER:
            h.state["lanes"][lane_name]["worktree"] = lane_name
            h.state["lanes"][lane_name]["state"] = "NEEDS_SOL_REVIEW"
            h.state["lanes"][lane_name]["review"] = {
                "type": "POST_PHASE_CHECKPOINT"
                if lane_name == "authority" else "CURRENT_PHASE_REVIEW"
            }
            h.state["lanes"][lane_name]["worker_pid"] = None
        h.git_dirty = lambda worktree: worktree != "authority"
        h.git_status_details = lambda _worktree: (True, [], "")
        with tempfile.TemporaryDirectory(prefix="skyrim-product-") as product_root, \
                tempfile.TemporaryDirectory(prefix="skyrim-self-test-state-") as state_root:
            for name in ("PRODUCT_VISION.md", "WORLD_RULES.md", "MILESTONE_01_CORE_WORLD.md"):
                (Path(product_root) / name).write_text("test product context\n", encoding="utf-8")
            h.config["product_root"] = product_root
            with patch.object(supervisor, "STATE_DIR", Path(state_root)):
                self.assertEqual(h.self_test(), 0)

    def test_self_test_accepts_paused_current_review_diffs(self) -> None:
        h = LiveWorkerObserverHarness()
        for lane_name in supervisor.LANE_ORDER:
            h.state["lanes"][lane_name]["worktree"] = lane_name
            h.state["lanes"][lane_name]["state"] = (
                "NEEDS_SOL_REVIEW" if lane_name == "authority" else "PAUSED"
            )
            h.state["lanes"][lane_name]["review"] = {
                "type": "POST_PHASE_CHECKPOINT"
                if lane_name == "authority" else "CURRENT_PHASE_REVIEW"
            }
            h.state["lanes"][lane_name]["worker_pid"] = None
        h.git_dirty = lambda worktree: worktree != "authority"
        h.git_status_details = lambda _worktree: (True, [], "")
        with tempfile.TemporaryDirectory(prefix="skyrim-product-") as product_root, \
                tempfile.TemporaryDirectory(prefix="skyrim-self-test-state-") as state_root:
            for name in ("PRODUCT_VISION.md", "WORLD_RULES.md", "MILESTONE_01_CORE_WORLD.md"):
                (Path(product_root) / name).write_text("test product context\n", encoding="utf-8")
            h.config["product_root"] = product_root
            with patch.object(supervisor, "STATE_DIR", Path(state_root)):
                self.assertEqual(h.self_test(), 0)

    def test_observer_prepare_does_not_reconcile_persisted_coding_lanes(self) -> None:
        h = LiveWorkerObserverHarness()
        before = {
            lane_name: {
                "state": lane["state"], "worker_pid": lane["worker_pid"],
                "recovery_attempts": lane["recovery_attempts"],
            }
            for lane_name, lane in h.state["lanes"].items()
        }
        probe_before = h.state["codex_availability"]["probe_started_at"]
        supervisor.Supervisor._prepare_state(h)
        after = {
            lane_name: {
                "state": lane["state"], "worker_pid": lane["worker_pid"],
                "recovery_attempts": lane["recovery_attempts"],
            }
            for lane_name, lane in h.state["lanes"].items()
        }
        self.assertEqual(after, before)
        self.assertEqual(h.state["codex_availability"]["probe_started_at"], probe_before)

    def test_runtime_owner_startup_reconciles_untouched_stale_coding_lanes(self) -> None:
        h = LiveWorkerObserverHarness()
        h.runtime_owner = True
        h._state_write_enabled = True
        for lane_name, lane in h.state["lanes"].items():
            worktree = f"/tmp/{lane_name}"
            lane["worktree"] = worktree
            lane["history"] = [{"phase": "PREVIOUS", "commit": "a" * 40}]
            lane["last_commit"] = "a" * 40
            h.config["lanes"][lane_name]["worktree"] = worktree
        h.git_head = lambda _worktree: "a" * 40
        h.git_branch = lambda worktree: next(
            config["branch"] for config in h.config["lanes"].values()
            if config["worktree"] == worktree
        )
        h.git_status_details = lambda _worktree: (True, [], "")
        supervisor.Supervisor._prepare_state(h)
        for lane in h.state["lanes"].values():
            self.assertEqual(lane["state"], "READY")
            self.assertIsNone(lane["worker_pid"])
            self.assertEqual(lane["recovery_attempts"], 0)

    def test_daemon_lock_is_acquired_before_runtime_owner_construction(self) -> None:
        with tempfile.TemporaryDirectory(prefix="skyrim-lock-test-") as root:
            root_path = Path(root)
            blocked_fcntl = SimpleNamespace(
                LOCK_EX=1, LOCK_NB=2,
                flock=unittest.mock.Mock(side_effect=BlockingIOError),
            )
            with patch.object(supervisor, "STATE_DIR", root_path), \
                    patch.object(supervisor, "LOCK_PATH", root_path / "supervisor.lock"), \
                    patch.object(supervisor, "fcntl", blocked_fcntl), \
                    patch.object(supervisor, "Supervisor") as constructor:
                self.assertEqual(supervisor.run_daemon(), 2)
            constructor.assert_not_called()

    def test_operator_command_does_not_reconcile_unrelated_live_worker(self) -> None:
        h = LiveWorkerObserverHarness()
        authority_before = ownership_snapshot(h)["lanes"]["authority"]
        self.assertEqual(h.block_lane("combat"), 0)
        self.assertEqual(h.state["lanes"]["combat"]["worker_pid"], 111)
        self.assertEqual(ownership_snapshot(h)["lanes"]["authority"], authority_before)
        self.assertEqual(h.state["codex_availability"]["probe_started_at"], "2026-09-22T00:01:10+00:00")

    def test_worker_prompt_requires_local_authoritative_source_access(self) -> None:
        h = Harness()
        h.state["lanes"]["combat"] = {
            "state": "READY", "branch": "main", "worktree": "/scratch/combat",
            "plan": "/scratch/combat/PLAN.md", "phase_index": 0,
            "phase_id": "C01", "phase_title": "phase",
            "plan_data": {"phases": [{"id": "C01", "title": "phase"}], "boundaries": []},
            "review": {}, "review_reasons": [],
        }
        h.roadmap_snapshot = None
        prompt = h.worker_prompt("combat")
        self.assertIn("local lane worktree is the authoritative source tree", prompt)
        self.assertIn("github connectors or web search", prompt.lower())
        self.assertIn("Do not mutate Git metadata", prompt)

    def test_worker_environment_hides_github_and_ssh_credentials(self) -> None:
        h = Harness()
        environment = h.worker_env()
        for key in (
            "GH_TOKEN", "GITHUB_TOKEN", "GH_ENTERPRISE_TOKEN",
            "GITHUB_ENTERPRISE_TOKEN", "SSH_AUTH_SOCK", "SSH_AGENT_PID",
        ):
            self.assertNotIn(key, environment)

    def test_worker_smoke_command_uses_only_disposable_workspace(self) -> None:
        h = Harness()
        scratch = Path("/var/lib/skyrim-dev/smoke/test-only")
        command = h.worker_smoke_command(scratch)
        self.assertIn("--sandbox", command)
        self.assertEqual(command[command.index("--model") + 1], "gpt-6-luna")
        self.assertIn('model_reasoning_effort="max"', command)
        self.assertIn("workspace-write", command)
        self.assertIn("--cd", command)
        self.assertIn(str(scratch), command)
        self.assertIn("--skip-git-repo-check", command)
        self.assertNotIn("--dangerously-bypass-approvals-and-sandbox", command)
        self.assertNotIn("/srv/projects/skyrim-online-str/workers/combat", command)

    def test_worker_smoke_input_matches_the_exact_prompt_content(self) -> None:
        harness = Harness()
        observed = {}

        class FakeProcess:
            def __init__(self, _command, *, cwd, **_kwargs):
                scratch = Path(cwd)
                observed["input"] = (scratch / "input.txt").read_bytes()
                if observed["input"] == b"harmless smoke input":
                    (scratch / "output.txt").write_bytes(b"smoke-write-ok")
                self.returncode = 0

            def communicate(self, timeout=None):
                return "WORKER_OPERATOR_INBOX: BLOCKED\n", None

        with tempfile.TemporaryDirectory(prefix="worker-smoke-fixture-") as scratch_root:
            harness.config["worker_smoke_root"] = scratch_root
            with patch.object(supervisor.subprocess, "Popen", FakeProcess):
                result = harness.worker_smoke_test()

        self.assertEqual(observed["input"], b"harmless smoke input")
        self.assertEqual(result, 0)

    def test_worker_smoke_does_not_change_protected_heads_or_worktrees(self) -> None:
        h = LiveWorkerObserverHarness()
        before = ownership_snapshot(h)
        self.assertEqual(h.config["lanes"]["combat"]["worktree"], "")
        self.assertEqual(ownership_snapshot(h), before)


class V35EmptyCurrentPhaseReviewTests(unittest.TestCase):
    def _current_lane(self, root: Path, state: str = "NEEDS_SOL_REVIEW") -> tuple[Harness, dict, str]:
        harness = Harness()
        lane = configure_combat_lane(harness, root, state)
        head = subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
        ).strip()
        lane.update({
            "phase_id": "C08", "phase_title": "current phase", "phase_index": 7,
            "last_commit": head, "history": [{"phase": "C07", "commit": head}],
            "scheduler_task_id": "C08", "worker_phase_id": None,
            "worker_start_head": None, "worker_exit_code": None,
            "worker_interrupted": False, "worker_result": None,
            "review": {
                "type": "CURRENT_PHASE_REVIEW",
                "reviewed_phase": {"id": "C08", "title": "current phase"},
                "commit_sha": head, "next_phase": {"id": "C08", "action": "retry_current_phase"},
            },
            "review_reasons": ["empty current-phase evidence"],
            "ci": {"status": "NOT_RUN", "sha": head},
        })
        harness.state["architect_review"] = {
            "enabled": True, "active_review_id": None, "queue": [], "items": {},
            "evidence_errors": {}, "bundle_failures": {},
        }
        return harness, lane, head

    def _patch_review_packet(self, harness: Harness, root: Path) -> None:
        harness.make_review_packet = lambda *_args, **_kwargs: root / "review-packet.md"

    def test_prior_phase_head_is_empty_and_stale_gate_is_retired(self) -> None:
        root = make_git_repo()
        harness, lane, head = self._current_lane(root)
        harness.state["architect_review"]["queue"] = ["stale-review"]
        harness.state["architect_review"]["items"] = {
            "stale-review": {
                "lane": "combat", "phase": "C08", "reviewed_sha": head,
                "status": "EVIDENCE_ERROR", "review_type": "CURRENT_PHASE_REVIEW",
            },
        }
        harness.state["architect_review"]["evidence_errors"] = {
            "combat:C08": {
                "lane": "combat", "phase": "C08", "reviewed_sha": head,
                "review_type": "CURRENT_PHASE_REVIEW", "status": "REQUIRES_INFRA_REVIEW",
            },
        }

        evidence = harness._current_phase_review_evidence("combat")
        self.assertEqual(evidence["kind"], "empty")
        self.assertEqual(evidence["reason_code"], "NO_CURRENT_PHASE_EVIDENCE")
        self.assertEqual(harness.reconcile_invalid_empty_current_phase_reviews(), 1)

        self.assertEqual(lane["state"], "READY")
        self.assertEqual(lane["ci"], {
            "status": "NOT_RUN", "sha": head, "run_id": None, "url": None,
        })
        self.assertIsNone(lane["worker_result"])
        self.assertIsNone(lane["review"]["type"])
        self.assertEqual(harness.state["architect_review"]["queue"], [])
        self.assertEqual(harness.state["architect_review"]["items"]["stale-review"]["status"], "STALE")
        self.assertEqual(harness.state["architect_review"]["evidence_errors"]["combat:C08"]["status"], "STALE")
        self.assertEqual(harness.state["architect_review"]["stale_current_phase_reviews"][0]["head"], head)

    def test_untouched_phase_cannot_create_or_queue_current_phase_review(self) -> None:
        root = make_git_repo()
        harness, lane, _head = self._current_lane(root, "LOCAL_VALIDATION")
        harness._control_plane_valid = lambda: True

        supervisor.Supervisor.review(
            harness, "combat", ["validation requires a review"], notify_external=False,
        )

        self.assertEqual(lane["state"], "READY")
        self.assertIsNone(lane["review"].get("type"))
        self.assertEqual(harness.queue_sol_reviews(), 0)
        self.assertEqual(harness.state["architect_review"]["queue"], [])

    def test_completed_checkpoint_approval_advances_next_phase_ready_and_keeps_history(self) -> None:
        root = make_git_repo()
        harness, lane, head = self._current_lane(root)
        lane.update({
            "state": "NEEDS_SOL_REVIEW", "phase_id": "C07", "phase_index": 6,
            "worker_phase_id": "C07", "worker_start_head": head,
            "worker_exit_code": 0, "worker_result": "COMPLETE",
            "review": {
                "type": "POST_PHASE_CHECKPOINT",
                "reviewed_phase": {"id": "C07", "title": "completed phase"},
                "commit_sha": head, "next_phase": {"id": "C08", "title": "current phase"},
                "selected_model": "gpt-6-luna", "selected_reasoning_effort": "high",
            },
        })
        harness._refresh_plan = lambda _name, target: target.update({
            "phase_id": "C08", "phase_title": "current phase",
        })

        self.assertEqual(supervisor.Supervisor.approve_review(harness, "combat"), 0)

        self.assertEqual(lane["state"], "READY")
        self.assertEqual(lane["phase_id"], "C08")
        self.assertIsNone(lane["worker_result"])
        self.assertIsNone(lane["worker_phase_id"])
        self.assertFalse(lane["worker_interrupted"])
        self.assertEqual(lane["ci"]["status"], "NOT_RUN")
        self.assertEqual(lane["review"]["decision"], "APPROVED_ADVANCE_ONCE")
        self.assertEqual(lane["review"]["selected_reasoning_effort"], "high")

    def test_interrupted_dirty_worker_is_recoverable_and_preserves_bytes(self) -> None:
        root = make_git_repo()
        harness, lane, head = self._current_lane(root, "CODING")
        changed = root / "tracked.txt"
        changed.write_text("preserved interrupted work\n", encoding="utf-8")
        before = changed.read_bytes()
        lane.update({
            "worker_phase_id": "C08", "worker_start_head": head,
            "worker_exit_code": -15, "worker_interrupted": True,
            "worker_log": str(root / "worker.log"),
        })
        lane["review"]["type"] = None

        class TerminatedProcess:
            pid = 10
            returncode = None

            def terminate(self):
                self.returncode = -15

            def wait(self, timeout=None):
                return self.returncode

        harness.processes["combat"] = TerminatedProcess()
        harness._recompute_scheduler = lambda: None
        harness.shutdown()

        self.assertEqual(lane["state"], "RECOVERING")
        self.assertEqual(lane["recovery_attempts"], 1)
        self.assertEqual(changed.read_bytes(), before)
        self.assertNotIn("combat", harness.processes)

    def test_interrupted_clean_worker_returns_untouched_phase_to_ready(self) -> None:
        root = make_git_repo()
        harness, lane, head = self._current_lane(root, "CODING")
        lane.update({
            "worker_phase_id": "C08", "worker_start_head": head,
            "worker_exit_code": -15, "worker_interrupted": True,
        })

        harness._reconcile_interrupted_worker("combat", lane)

        self.assertEqual(lane["state"], "READY")
        self.assertEqual(lane["recovery_attempts"], 0)
        self.assertIsNone(lane["worker_result"])

    def test_current_phase_committed_diff_can_still_request_review(self) -> None:
        root = make_git_repo()
        harness, lane, base = self._current_lane(root, "LOCAL_VALIDATION")
        (root / "phase.txt").write_text("current phase implementation\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(root), "add", "phase.txt"], check=True)
        subprocess.run([
            "git", "-C", str(root), "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
            "commit", "-qm", "C08 implementation",
        ], check=True)
        head = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
        lane["last_commit"] = head
        self._patch_review_packet(harness, root)

        evidence = harness._current_phase_review_evidence("combat")
        supervisor.Supervisor.review(
            harness, "combat", ["committed current-phase work needs review"], notify_external=False,
        )

        self.assertEqual(evidence["kind"], "reviewable")
        self.assertEqual(evidence["reason_code"], "CURRENT_PHASE_COMMITTED_DIFF")
        self.assertEqual(lane["state"], "NEEDS_SOL_REVIEW")
        self.assertEqual(lane["review"]["commit_sha"], head)
        self.assertEqual(lane["review"]["selected_model"], "gpt-6-luna")
        self.assertEqual(lane["review"]["selected_reasoning_effort"], "max")
        self.assertEqual(len(lane["history"]), 1)
        self.assertNotEqual(base, head)

    def test_current_phase_worker_result_can_still_request_review_without_diff(self) -> None:
        root = make_git_repo()
        harness, lane, head = self._current_lane(root, "LOCAL_VALIDATION")
        lane.update({
            "worker_phase_id": "C08", "worker_start_head": head,
            "worker_exit_code": 0, "worker_interrupted": False,
            "worker_result": "NEEDS_SOL_REVIEW",
        })
        self._patch_review_packet(harness, root)

        evidence = harness._current_phase_review_evidence("combat")
        supervisor.Supervisor.review(
            harness, "combat", ["worker explicitly requested a decision"], notify_external=False,
        )

        self.assertEqual(evidence["kind"], "reviewable")
        self.assertEqual(evidence["reason_code"], "CURRENT_PHASE_WORKER_RESULT")
        self.assertEqual(lane["state"], "NEEDS_SOL_REVIEW")
        self.assertEqual(lane["review"]["selected_model"], "gpt-6-luna")
        self.assertEqual(lane["review"]["selected_reasoning_effort"], "max")

    def test_oversized_finished_worker_diff_is_reviewable(self) -> None:
        root = make_git_repo()
        harness, lane, head = self._current_lane(root, "LOCAL_VALIDATION")
        (root / "tracked.txt").write_text("changed\n", encoding="utf-8")
        harness.config["max_changed_files"] = 0
        lane.update({
            "worker_phase_id": "C08", "worker_start_head": head,
            "worker_exit_code": 0, "worker_interrupted": False,
            "worker_result": "COMPLETE_WITH_VALIDATION_GAP",
        })

        evidence = harness._current_phase_review_evidence("combat")

        self.assertEqual(evidence["kind"], "reviewable")
        self.assertEqual(evidence["reason_code"], "CURRENT_PHASE_OVERSIZED_DIFF")

    def test_oversized_interrupted_worker_diff_stays_fail_closed(self) -> None:
        root = make_git_repo()
        harness, lane, head = self._current_lane(root, "LOCAL_VALIDATION")
        (root / "tracked.txt").write_text("changed\n", encoding="utf-8")
        harness.config["max_changed_files"] = 0
        lane.update({
            "worker_phase_id": "C08", "worker_start_head": head,
            "worker_exit_code": -15, "worker_interrupted": True,
        })

        evidence = harness._current_phase_review_evidence("combat")

        self.assertEqual(evidence["kind"], "unknown")
        self.assertEqual(evidence["reason_code"], "RECOVERY_DIFF_UNSAFE")

    def _oversized_blocked_lane(self, root: Path, interrupted: bool) -> tuple[Harness, dict]:
        harness, lane, head = self._current_lane(root, "BLOCKED")
        (root / "tracked.txt").write_text("changed\n", encoding="utf-8")
        harness.config["max_changed_files"] = 0
        harness.runtime_owner = True
        lane.update({
            "worker_phase_id": "C08", "worker_start_head": head,
            "worker_exit_code": -15 if interrupted else 0, "worker_interrupted": interrupted,
            "worker_pid": None,
            "last_error": "current-phase review evidence is ambiguous; state preserved",
        })
        harness.state["architect_review"]["reconciliation_errors"] = {
            "combat:C08:RECOVERY_DIFF_UNSAFE": {
                "lane": "combat", "phase": "C08", "status": "REQUIRES_INFRA_REVIEW",
                "reason_code": "RECOVERY_DIFF_UNSAFE",
                "reasons": ["changed file count 1 exceeds configured limit 0"],
            },
        }
        return harness, lane

    def test_oversized_blocked_lane_is_rerouted_to_review(self) -> None:
        root = make_git_repo()
        harness, lane = self._oversized_blocked_lane(root, interrupted=False)

        self.assertEqual(harness.reconcile_oversized_blocked_current_phase_reviews(), 1)

        self.assertEqual(lane["state"], "NEEDS_SOL_REVIEW")
        self.assertEqual(harness.review_calls[-1][1], ["changed file count 1 exceeds configured limit 0"])
        failure = harness.state["architect_review"]["reconciliation_errors"]["combat:C08:RECOVERY_DIFF_UNSAFE"]
        self.assertEqual(failure["status"], "RESOLVED_OVERSIZED_REVIEWABLE")
        self.assertTrue((root / "tracked.txt").read_text(encoding="utf-8") == "changed\n")

    def test_oversized_blocked_interrupted_lane_stays_blocked(self) -> None:
        root = make_git_repo()
        harness, lane = self._oversized_blocked_lane(root, interrupted=True)

        self.assertEqual(harness.reconcile_oversized_blocked_current_phase_reviews(), 0)

        self.assertEqual(lane["state"], "BLOCKED")
        self.assertEqual(harness.review_calls, [])


class V34ReviewerRoutingTests(unittest.TestCase):
    def test_ordinary_review_routes_to_luna_max(self) -> None:
        h = Harness()
        tier, reason = h._review_tier_for_request(
            "combat", "POST_PHASE_CHECKPOINT", ["ordinary implementation review"]
        )
        self.assertEqual((tier, reason), ("normal", None))
        self.assertEqual(h._review_route(tier), {
            "model": "gpt-6-luna", "reasoning_effort": "max", "family": "luna",
        })

    def test_ordinary_review_never_inherits_architect_sol_route(self) -> None:
        h = Harness()
        h.config["architect_review"] = {"model": "gpt-6-sol", "reasoning_effort": "max"}
        self.assertEqual(h._review_route("normal")["model"], "gpt-6-luna")
        self.assertEqual(h._review_route("normal")["reasoning_effort"], "max")
        self.assertEqual(h._review_route("architect")["model"], "gpt-6-sol")
        self.assertEqual(h._review_route("architect")["reasoning_effort"], "medium")

    def test_explicit_architect_escalation_routes_sol_medium(self) -> None:
        h = Harness()
        tier, reason = h._review_tier_for_request(
            "authority", "FINAL_MILESTONE_OR_QUEUE_REVIEW", ["queue complete"]
        )
        self.assertEqual(tier, "architect")
        self.assertIn("final integration/milestone", reason or "")
        self.assertEqual(h._review_route("architect")["model"], "gpt-6-sol")
        self.assertEqual(h._review_route("architect")["reasoning_effort"], "medium")

    def test_direct_architecture_boundaries_escalate_and_validation_gap_does_not(self) -> None:
        h = Harness()
        tier, _ = h._review_tier_for_request(
            "authority", "CURRENT_PHASE_REVIEW",
            ["persistent database/schema or migration surface changed"],
        )
        self.assertEqual(tier, "architect")
        tier, _ = h._review_tier_for_request(
            "combat", "POST_PHASE_CHECKPOINT",
            ["phase size validation gap was recorded; exact CI passed"],
        )
        self.assertEqual(tier, "normal")

    def test_sol_unavailability_waits_only_architect_lane(self) -> None:
        h = Harness()
        h.review_process = None
        h.review_output_thread = None
        h.review_log_path = None
        h.active_review_id = None
        h.state["architect_review"] = {
            "enabled": True, "queue": ["architect-item", "normal-item"],
            "items": {}, "phase_counters": {},
            "tier_availability": {
                "normal": h._availability_record(),
                "architect": h._availability_record(),
            },
        }
        h.state["lanes"] = {
            "authority": {"state": "NEEDS_SOL_REVIEW", "review": {"review_tier": "architect"}},
            "combat": {"state": "NEEDS_SOL_REVIEW", "review": {"review_tier": "normal"}},
        }
        architect_item = {
            "review_id": "architect-item", "lane": "authority", "review_tier": "architect",
            "status": "RUNNING", "failure_count": 0,
        }
        normal_item = {
            "review_id": "normal-item", "lane": "combat", "review_tier": "normal",
            "status": "QUEUED", "failure_count": 0,
        }
        h.state["architect_review"]["items"] = {
            "architect-item": architect_item, "normal-item": normal_item,
        }
        h._review_failure(architect_item, "model unavailable", model_unavailable=True)
        self.assertEqual(architect_item["status"], "WAITING_FOR_ARCHITECT_MODEL")
        self.assertEqual(h.state["lanes"]["authority"]["state"], "WAITING_FOR_ARCHITECT_MODEL")
        self.assertEqual(h.state["lanes"]["combat"]["state"], "NEEDS_SOL_REVIEW")
        self.assertEqual(normal_item["status"], "QUEUED")
        self.assertEqual(
            h.state["architect_review"]["tier_availability"]["architect"]["status"],
            "MODEL_UNAVAILABLE",
        )
        self.assertEqual(
            h.state["architect_review"]["tier_availability"]["normal"]["status"],
            "AVAILABLE",
        )

    def test_unconfigured_luna_fallback_is_not_assumed(self) -> None:
        h = Harness()
        h.config["model_routing"] = {
            "development": {"model": "gpt-6-luna", "family": "luna", "reasoning_effort": "max"},
            "review": {"normal": {"model": "gpt-6-luna", "family": "luna", "reasoning_effort": "max"}},
            "luna_fallback_models": [],
        }
        availability = h._availability_record()
        with patch.object(h, "_probe_model_route", return_value=(False, "model unavailable")) as probe:
            self.assertIsNone(h._resolve_luna_route("review", availability))
        probe.assert_called_once()

    def test_configured_luna_fallback_requires_successful_read_only_probe(self) -> None:
        h = Harness()
        h.config["model_routing"] = {
            "development": {"model": "gpt-6-luna", "family": "luna", "reasoning_effort": "max"},
            "review": {"normal": {"model": "gpt-6-luna", "family": "luna", "reasoning_effort": "max"}},
            "luna_fallback_models": [{"model": "gpt-6-luna-reserve", "family": "luna", "reasoning_effort": "max"}],
        }
        availability = h._availability_record()
        with patch.object(h, "_probe_model_route", side_effect=[(False, "primary unavailable"), (True, "MODEL_OK")]) as probe:
            route = h._resolve_luna_route("review", availability)
        self.assertEqual(route["model"], "gpt-6-luna-reserve")
        self.assertEqual(route["reasoning_effort"], "max")
        self.assertTrue(availability["fallback_used"])
        self.assertEqual(probe.call_count, 2)
        with patch.object(h, "_probe_model_route", side_effect=[(False, "fallback unavailable"), (True, "MODEL_OK")]) as probe:
            route = h._resolve_luna_route("review", availability)
        self.assertEqual(route["model"], "gpt-6-luna")
        self.assertEqual(probe.call_count, 2)

    def test_legacy_queued_review_is_reclassified_and_old_bundle_not_launched(self) -> None:
        h = Harness()
        h._reconcile_architect_review_runtime = lambda: None
        h.config["model_routing"] = {
            "development": {"model": "gpt-6-luna", "family": "luna", "reasoning_effort": "max"},
            "review": {
                "normal": {"model": "gpt-6-luna", "family": "luna", "reasoning_effort": "max"},
                "architect": {"model": "gpt-6-sol", "family": "sol", "reasoning_effort": "max"},
            },
            "luna_fallback_models": [],
        }
        legacy_id = "legacy-sol-bundle"
        h.state.update({
            "lanes": {"authority": {
                "state": "NEEDS_SOL_REVIEW", "phase_id": "A09", "review_reasons": ["ordinary phase review"],
                "review": {"type": "CURRENT_PHASE_REVIEW", "reviewed_phase": {"id": "A09"}, "commit_sha": "a" * 40},
            }},
            "architect_review": {
                "enabled": True, "queue": [legacy_id],
                "items": {legacy_id: {
                    "review_id": legacy_id, "lane": "authority", "phase": "A09",
                    "review_type": "CURRENT_PHASE_REVIEW", "reviewed_sha": "a" * 40, "status": "QUEUED",
                }},
            },
            "scheduler": {"tasks": {}},
        })
        observer = Harness()
        observer.runtime_owner = False
        observer.config["model_routing"] = deepcopy(h.config["model_routing"])
        observer.state = deepcopy(h.state)
        observer._prepare_architect_review_state()
        self.assertEqual(observer.state["architect_review"]["items"][legacy_id]["status"], "QUEUED")
        self.assertEqual(observer.state["architect_review"]["queue"], [legacy_id])
        self.assertEqual(observer.state["architect_review"]["items"][legacy_id]["review_tier"], "normal")

        h._prepare_architect_review_state()
        item = h.state["architect_review"]["items"][legacy_id]
        lane_review = h.state["lanes"]["authority"]["review"]
        self.assertEqual(item["status"], "STALE")
        self.assertEqual(item["review_tier"], "normal")
        self.assertEqual(lane_review["review_tier"], "normal")
        self.assertEqual(lane_review["selected_model"], "gpt-6-luna")
        self.assertEqual(lane_review["selected_reasoning_effort"], "max")
        self.assertEqual(h.state["architect_review"]["queue"], [])

    def test_model_availability_backoff_survives_json_state_restart(self) -> None:
        h = Harness()
        h.runtime_owner = False
        architect_retry = "2030-01-01T00:00:00+00:00"
        luna_retry = "2030-01-01T00:15:00+00:00"
        h.state = json.loads(json.dumps({
            "global_mode": "RUNNING",
            "lanes": {"authority": {
                "state": "WAITING_FOR_ARCHITECT_MODEL",
                "review": {"review_tier": "architect", "type": "CURRENT_PHASE_REVIEW"},
            }},
            "architect_review": {
                "enabled": True, "queue": [], "items": {
                    "arch-wait": {"lane": "authority", "review_tier": "architect",
                                  "status": "WAITING_FOR_ARCHITECT_MODEL", "next_retry_at": architect_retry},
                },
                "availability": {"status": "RATE_LIMITED", "retry_count": 4,
                                 "backoff_seconds": 3600, "next_retry_at": architect_retry},
                "tier_availability": {
                    "normal": {"status": "MODEL_UNAVAILABLE", "retry_count": 2,
                               "backoff_seconds": 1800, "next_retry_at": luna_retry},
                    "architect": {"status": "RATE_LIMITED", "retry_count": 4,
                                  "backoff_seconds": 3600, "next_retry_at": architect_retry},
                },
                "normal_retry_counters": {"authority:A09": {"same_material_cycles": 1}},
            },
        }))
        h._prepare_architect_review_state()
        state = h.state["architect_review"]
        self.assertEqual(state["tier_availability"]["architect"]["next_retry_at"], architect_retry)
        self.assertEqual(state["tier_availability"]["normal"]["next_retry_at"], luna_retry)
        self.assertEqual(state["items"]["arch-wait"]["next_retry_at"], architect_retry)
        self.assertEqual(state["normal_retry_counters"]["authority:A09"]["same_material_cycles"], 1)
        self.assertEqual(state["availability"], state["tier_availability"]["architect"])

    def test_reviewer_cap_is_one_and_worker_cap_remains_two(self) -> None:
        h = Harness()
        h.config["architect_review"] = {"max_concurrent_reviewers": 1}
        h.config["max_concurrent_workers"] = 2
        self.assertEqual(h._reviewer_limit(), 1)
        self.assertEqual(h._worker_limit(), 2)

    def test_model_unavailable_classifier_is_separate_from_generic_failure(self) -> None:
        self.assertTrue(supervisor.classify_codex_model_unavailable(1, "unknown model gpt-6-luna"))
        self.assertTrue(supervisor.classify_codex_model_unavailable(1, "model access permission denied"))
        self.assertFalse(supervisor.classify_codex_model_unavailable(1, "pytest assertion failed"))
        self.assertFalse(supervisor.classify_codex_model_unavailable(0, "unknown model"))

    def test_starting_normal_review_builds_luna_max_command(self) -> None:
        h = Harness()
        h.review_process = None
        h.review_output_thread = None
        h.review_log_path = None
        h.active_review_id = None
        h._control_plane_valid = lambda: True
        with tempfile.TemporaryDirectory(prefix="review-route-test-") as root:
            root_path = Path(root)
            h._review_root = lambda: root_path
            review_id = "a" * 64
            bundle_dir = root_path / "bundles" / review_id
            bundle_dir.mkdir(parents=True)
            bundle_path = bundle_dir / "bundle.json"
            bundle = {
                "review_id": review_id, "review_state_sha256": "b" * 64,
                "lane": "combat", "phase": "C01", "review_type": "CURRENT_PHASE_REVIEW",
                "reviewed_sha": "c" * 40, "evidence_ids": ["bundle.diff"],
            }
            bundle_path.write_text(json.dumps(bundle), encoding="utf-8")
            h.state["lanes"] = {
                "combat": {"state": "NEEDS_SOL_REVIEW", "review": {"type": "CURRENT_PHASE_REVIEW"}},
            }
            architect_wait_id = "f" * 64
            h.state["architect_review"] = {
                "enabled": True, "queue": [architect_wait_id, review_id], "items": {},
                "phase_counters": {}, "active_review_id": None,
                "tier_availability": {
                    "normal": h._availability_record(),
                    "architect": {**h._availability_record(), "status": "RATE_LIMITED",
                                  "next_retry_at": time.time() + 3600, "backoff_seconds": 3600},
                },
            }
            h.state["architect_review"]["items"][architect_wait_id] = {
                "review_id": architect_wait_id, "lane": "authority", "phase": "A09",
                "review_type": "CURRENT_PHASE_REVIEW", "review_tier": "architect",
                "status": "QUEUED", "failure_count": 0,
            }
            item = {
                **bundle, "bundle_dir": str(bundle_dir), "bundle_path": str(bundle_path),
                "evidence_version": 3, "control_plane_sha": "d" * 40,
                "worker_attempt": 1, "status": "QUEUED", "review_tier": "normal",
                "launch_attempts": 0, "failure_count": 0,
            }
            h.state["architect_review"]["items"][review_id] = item
            h._validate_current_review_state = lambda _item: (True, "", bundle)
            runtime = root_path / "runtime"
            result_dir = runtime / "result"
            codex_home = runtime / "codex-home"
            result_dir.mkdir(parents=True)
            codex_home.mkdir()
            h._prepare_review_runtime = lambda _item: (runtime, result_dir, codex_home, root_path / "review.log")

            class FakeProcess:
                pid = 4321

                def __init__(self):
                    self.stdin = io.StringIO()
                    self.stdout = io.StringIO()

            process = FakeProcess()
            with patch.object(supervisor.subprocess, "Popen", return_value=process), \
                    patch.object(architect_review, "build_bwrap_command", return_value=["bwrap-test"]) as build:
                self.assertTrue(h.start_next_reviewer())
            self.assertEqual(item["selected_model"], "gpt-6-luna")
            self.assertEqual(item["selected_reasoning_effort"], "max")
            self.assertEqual(build.call_args.args[-2:], ("gpt-6-luna", "max"))
            self.assertNotEqual(build.call_args.args[-2], "gpt-6-sol")
            self.assertEqual(h.state["architect_review"]["queue"], [architect_wait_id])
            self.assertEqual(h.state["architect_review"]["items"][architect_wait_id]["status"], "QUEUED")


if __name__ == "__main__":
    unittest.main()
