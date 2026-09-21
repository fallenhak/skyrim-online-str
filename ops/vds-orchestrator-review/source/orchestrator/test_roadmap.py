from __future__ import annotations

import copy
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

import roadmap
import supervisor


def base_data() -> dict:
    return {
        "version": 1,
        "current_milestone": "M01",
        "scheduler_policy": {
            "max_concurrent_workers": 2,
            "dependency_fail_closed": True,
        },
        "milestones": [
            {
                "id": "M01",
                "title": "Core World",
                "status": "ACTIVE",
                "executable": True,
                "acceptance": ["Windows runtime evidence"],
                "workstreams": [
                    {
                        "id": "M01-COMBAT",
                        "lane": "combat",
                        "mode": "EXISTING_PLAN",
                        "start_phase": "C03",
                    },
                    {
                        "id": "M01-WORLD",
                        "lane": "world",
                        "mode": "ROADMAP_TASKS",
                        "status": "BLOCKED_DEPENDENCY",
                        "branch": None,
                        "blocked_by": ["M01-COMBAT"],
                        "external_gates": ["reviewed integration branch required"],
                        "tasks": [
                            {
                                "id": "W01",
                                "title": "Define encounter identity",
                                "risk": "medium",
                                "requires_sol_review": True,
                                "depends_on": [],
                            },
                            {
                                "id": "W02",
                                "title": "Track encounter membership",
                                "risk": "medium",
                                "requires_sol_review": False,
                                "depends_on": ["W01"],
                            },
                        ],
                    },
                ],
            },
            {
                "id": "M02",
                "title": "Future",
                "status": "PLANNED",
                "executable": False,
            },
        ],
    }


def snapshot(data: dict | None = None, sha: str = "sha-a") -> roadmap.RoadmapSnapshot:
    return roadmap.validate_roadmap(data or base_data(), source_sha=sha)


class RoadmapValidationTests(unittest.TestCase):
    def test_valid_roadmap_load(self) -> None:
        value = snapshot()
        self.assertEqual(value.current_milestone, "M01")
        self.assertEqual(set(value.tasks), {"W01", "W02"})
        self.assertEqual(value.tasks["W01"].source, "ROADMAP")

    def test_load_malformed_roadmap_rejected(self) -> None:
        with TemporaryDirectory() as directory:
            path = Path(directory) / "roadmap.json"
            path.write_text("{", encoding="utf-8")
            with self.assertRaises(roadmap.RoadmapValidationError):
                roadmap.load_roadmap(path)

    def test_duplicate_task_id_rejected(self) -> None:
        data = base_data()
        data["milestones"][0]["workstreams"][1]["tasks"][1]["id"] = "W01"
        with self.assertRaisesRegex(roadmap.RoadmapValidationError, "duplicate ID"):
            snapshot(data)

    def test_unknown_dependency_rejected(self) -> None:
        data = base_data()
        data["milestones"][0]["workstreams"][1]["tasks"][1]["depends_on"] = ["W99"]
        with self.assertRaisesRegex(roadmap.RoadmapValidationError, "unknown dependency"):
            snapshot(data)

    def test_dependency_cycle_rejected(self) -> None:
        data = base_data()
        data["milestones"][0]["workstreams"][1]["tasks"][0]["depends_on"] = ["W02"]
        with self.assertRaisesRegex(roadmap.RoadmapValidationError, "cycle"):
            snapshot(data)

    def test_future_executable_false_milestone_never_accepts_executable_task(self) -> None:
        data = base_data()
        data["milestones"][1]["workstreams"] = [
            {
                "id": "M02-FUTURE",
                "lane": "future",
                "mode": "ROADMAP_TASKS",
                "status": "PLANNED",
                "branch": None,
                "tasks": [{"id": "F01", "title": "future task"}],
            }
        ]
        with self.assertRaisesRegex(roadmap.RoadmapValidationError, "executable=false"):
            snapshot(data)

    def test_malformed_future_lane_is_rejected(self) -> None:
        data = base_data()
        data["milestones"][0]["workstreams"][1]["lane"] = "world"
        data["milestones"][0]["workstreams"][1]["branch"] = "not-explicitly-future"
        with self.assertRaisesRegex(roadmap.RoadmapValidationError, "future/nonexistent"):
            snapshot(data)


class DependencySchedulerTests(unittest.TestCase):
    def test_dependency_ready_transition(self) -> None:
        task = snapshot().tasks["W02"]
        state, reason = roadmap.dependency_evaluation(
            task,
            task_states={"W01": "DONE"},
            workstream_states={"M01-COMBAT": "DONE"},
            milestone_executable=True,
            control_plane_valid=True,
            resolved_external_gates=task.external_gates,
            global_mode="RUNNING",
            lane_available=True,
        )
        self.assertEqual(state, "READY")
        self.assertIn("all dependencies", reason)

    def test_blocked_dependency_reason(self) -> None:
        task = snapshot().tasks["W02"]
        state, reason = roadmap.dependency_evaluation(
            task,
            task_states={"W01": "BLOCKED_DEPENDENCY"},
            workstream_states={"M01-COMBAT": "DONE"},
            milestone_executable=True,
            control_plane_valid=True,
            resolved_external_gates=task.external_gates,
            global_mode="RUNNING",
            lane_available=True,
        )
        self.assertEqual(state, "BLOCKED_DEPENDENCY")
        self.assertEqual(reason, "waiting for W01")

    def test_blocked_external_gate_reason(self) -> None:
        task = snapshot().tasks["W01"]
        state, reason = roadmap.dependency_evaluation(
            task,
            task_states={},
            workstream_states={"M01-COMBAT": "DONE"},
            milestone_executable=True,
            control_plane_valid=True,
            global_mode="RUNNING",
            lane_available=True,
        )
        self.assertEqual(state, "BLOCKED_EXTERNAL_GATE")
        self.assertIn("reviewed integration branch required", reason)

    def test_pre_task_sol_gate(self) -> None:
        data = base_data()
        data["milestones"][0]["workstreams"][1]["external_gates"] = []
        task = snapshot(data).tasks["W01"]
        state, reason = roadmap.dependency_evaluation(
            task,
            task_states={},
            workstream_states={"M01-COMBAT": "DONE"},
            milestone_executable=True,
            control_plane_valid=True,
            global_mode="RUNNING",
            lane_available=True,
        )
        self.assertEqual(state, "NEEDS_SOL_REVIEW")
        self.assertEqual(reason, "PRE_TASK_ROADMAP_REVIEW required before implementation")

    def test_approval_is_bound_to_exact_sha_and_definition(self) -> None:
        data = base_data()
        data["milestones"][0]["workstreams"][1]["external_gates"] = []
        task = snapshot(data).tasks["W01"]
        approval = {
            "control_plane_sha": task.control_plane_sha,
            "definition_hash": task.definition_hash,
        }
        state, _ = roadmap.dependency_evaluation(
            task,
            task_states={},
            workstream_states={"M01-COMBAT": "DONE"},
            milestone_executable=True,
            control_plane_valid=True,
            global_mode="RUNNING",
            lane_available=True,
            approval=approval,
        )
        self.assertEqual(state, "READY")
        changed = dict(approval, definition_hash="different")
        state, _ = roadmap.dependency_evaluation(
            task,
            task_states={},
            workstream_states={"M01-COMBAT": "DONE"},
            milestone_executable=True,
            control_plane_valid=True,
            global_mode="RUNNING",
            lane_available=True,
            approval=changed,
        )
        self.assertEqual(state, "NEEDS_SOL_REVIEW")

    def test_round_robin_fairness_and_reboot_cursor(self) -> None:
        lane, cursor = roadmap.next_round_robin_lane(
            {"combat", "authority", "population", "ui"}, supervisor.LANE_ORDER, 0
        )
        self.assertEqual(lane, "combat")
        lane, cursor = roadmap.next_round_robin_lane(
            {"combat", "authority", "population", "ui"}, supervisor.LANE_ORDER, cursor
        )
        self.assertEqual(lane, "authority")
        reboot_lane, reboot_cursor = roadmap.next_round_robin_lane(
            {"combat", "authority", "population", "ui"}, supervisor.LANE_ORDER, cursor
        )
        self.assertEqual(reboot_lane, "population")
        self.assertEqual(reboot_cursor, 3)

    def test_rate_limit_prevents_scheduler_start(self) -> None:
        h = object.__new__(supervisor.Supervisor)
        h.state = {
            "global_mode": "RUNNING",
            "codex_availability": {
                "status": "RATE_LIMITED",
                "next_retry_at": "2999-01-01T00:00:00+00:00",
            },
            "scheduler": {"tasks": {}, "cursor": 0, "resolved_external_gates": []},
            "lanes": {lane: {"state": "READY"} for lane in supervisor.LANE_ORDER},
        }
        h.config = {"max_concurrent_workers": 2, "lanes": {}}
        h.processes = {}
        h._control_plane_valid = lambda: True
        h._recompute_scheduler = lambda: None
        h.start_worker = lambda *args, **kwargs: self.fail("worker must not start")
        supervisor.Supervisor.schedule(h)

    def test_existing_plan_start_positions_are_preserved(self) -> None:
        h = object.__new__(supervisor.Supervisor)
        h.roadmap_snapshot = snapshot()
        h.state = {
            "control_plane": {"applied_sha": "sha-a"},
            "lanes": {
                "combat": {
                    "phase_index": 2,
                    "plan_data": {
                        "phases": [
                            {"id": "C01", "title": "one"},
                            {"id": "C02", "title": "two"},
                            {"id": "C03", "title": "three"},
                        ],
                        "boundaries": [],
                    },
                },
            },
        }
        h.plan_for = lambda lane: h.state["lanes"][lane]["plan_data"] if lane == "combat" else {"phases": [], "boundaries": []}
        tasks = supervisor.Supervisor._existing_scheduled_tasks(h)
        self.assertIn("C03", tasks)
        self.assertEqual(h.state["lanes"]["combat"]["phase_index"], 2)


class ControlAndMilestoneTests(unittest.TestCase):
    def test_active_task_mutation_requires_review(self) -> None:
        old = snapshot()
        data = base_data()
        data["milestones"][0]["workstreams"][1]["tasks"][0]["title"] = "changed"
        new = snapshot(data, "sha-b")
        h = object.__new__(supervisor.Supervisor)
        h.state = {
            "scheduler": {
                "tasks": {"W01": {"state": "RUNNING"}},
            },
            "lanes": {},
        }
        h.config = {}
        reasons = supervisor.Supervisor._control_change_requires_review(
            h, old, new, {"WORLD_RULES.md": "same"}, {"WORLD_RULES.md": "same"}
        )
        self.assertTrue(any("active task definition changed" in item for item in reasons))

    def test_world_rules_change_requires_review(self) -> None:
        h = object.__new__(supervisor.Supervisor)
        h.state = {"scheduler": {"tasks": {}}, "lanes": {}}
        h.config = {}
        reasons = supervisor.Supervisor._control_change_requires_review(
            h,
            snapshot(),
            snapshot(copy.deepcopy(base_data()), "sha-b"),
            {"WORLD_RULES.md": "old"},
            {"WORLD_RULES.md": "new"},
        )
        self.assertTrue(any("WORLD_RULES.md" in item for item in reasons))

    def test_invalid_candidate_does_not_replace_last_valid_snapshot(self) -> None:
        old = snapshot()
        with self.assertRaises(roadmap.RoadmapValidationError):
            bad = base_data()
            bad["milestones"][0]["workstreams"][1]["tasks"][1]["depends_on"] = ["missing"]
            snapshot(bad)
        self.assertEqual(old.current_milestone, "M01")
        self.assertEqual(set(old.tasks), {"W01", "W02"})

    def test_engineering_completion_does_not_equal_acceptance(self) -> None:
        h = object.__new__(supervisor.Supervisor)
        h.state = {
            "scheduler": {
                "milestones": {
                    "M01": {"state": "ENGINEERING_COMPLETE", "title": "Core World"}
                }
            }
        }
        h._recompute_scheduler = lambda: None
        h.save_state = lambda: None
        h._audit = lambda *args, **kwargs: None
        result = supervisor.Supervisor.accept_milestone(h, "M01", {})
        self.assertEqual(result, 1)
        self.assertEqual(h.state["scheduler"]["milestones"]["M01"]["state"], "ENGINEERING_COMPLETE")

    def test_explicit_milestone_acceptance_transition(self) -> None:
        h = object.__new__(supervisor.Supervisor)
        h.state = {
            "scheduler": {
                "milestones": {
                    "M01": {"state": "WAITING_RUNTIME_ACCEPTANCE", "title": "Core World"}
                }
            }
        }
        h._recompute_scheduler = lambda: None
        h.save_state = lambda: None
        h._audit = lambda *args, **kwargs: None
        evidence = {
            "runtime_platform": "Windows Skyrim",
            "clients": 2,
            "character_select": True,
            "humanoid_suppression": True,
            "synchronized_creature_encounter": True,
            "encounter_clear_reset_reentry": True,
            "stale_incarnation_rejection": True,
            "reconnect_server_character": True,
            "human_reviewed": True,
            "reviewed_by": "operator",
        }
        self.assertEqual(supervisor.Supervisor.accept_milestone(h, "M01", evidence), 0)
        self.assertEqual(h.state["scheduler"]["milestones"]["M01"]["state"], "ACCEPTED")


if __name__ == "__main__":
    unittest.main()
