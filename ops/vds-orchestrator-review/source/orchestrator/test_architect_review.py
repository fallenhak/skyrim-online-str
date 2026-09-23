from __future__ import annotations

import json
import unittest
from copy import deepcopy

from architect_review import (
    REVIEW_OUTPUT_SCHEMA,
    ReviewOutputError,
    build_bwrap_command,
    canonical_json,
    coalesce_queued_review_items,
    LEGACY_SEVERITY_BLOCK_REASON,
    normalize_review_evidence,
    should_recheck_legacy_blocked_retry,
    review_failure_fingerprint,
    review_identity,
    review_prompt,
    sha256_json,
    stable_review_state_sha256,
    validate_review_output,
    validate_tool_free_jsonl,
)


EXPECTED = {
    "review_id": "a" * 64,
    "lane": "authority",
    "phase": "A06",
    "review_type": "CURRENT_PHASE_REVIEW",
    "reviewed_sha": "0123456789abcdef01234567",
    "review_state_sha256": "b" * 64,
}
EVIDENCE = ["bundle.diff", "ci:Build linux"]
BUNDLE = {**EXPECTED, "evidence_ids": EVIDENCE, "worktree_evidence": {"diff_exact": "synthetic"}}


def payload(decision="RETRY"):
    return {
        "schema_version": 1,
        **EXPECTED,
        "decision": decision,
        "confidence": "high",
        "findings": [{
            "severity": "medium", "title": "Header is not self-contained",
            "details": "The public header uses uint32_t without directly including cstdint.",
            "evidence_refs": ["bundle.diff"],
        }],
        "required_actions": ([{
            "action": "Add the direct standard header include required by the public declaration, then rebuild both CI targets.",
            "evidence_refs": ["bundle.diff", "ci:Build linux"],
        }] if decision == "RETRY" else []),
        "reason": "The changed header depends on an undeclared transitive include.",
        "evidence_refs": ["bundle.diff"],
    }


class ReviewValidationTests(unittest.TestCase):
    def test_01_valid_retry_schema(self):
        got = validate_review_output(canonical_json(payload()), EXPECTED, EVIDENCE)
        self.assertEqual(got["decision"], "RETRY")

    def test_02_valid_approve_schema(self):
        got = validate_review_output(canonical_json(payload("APPROVE")), EXPECTED, EVIDENCE)
        self.assertEqual(got["decision"], "APPROVE")

    def test_03_valid_block_schema(self):
        got = validate_review_output(canonical_json(payload("BLOCK")), EXPECTED, EVIDENCE)
        self.assertEqual(got["decision"], "BLOCK")

    def test_04_rejects_non_json(self):
        with self.assertRaises(ReviewOutputError):
            validate_review_output("not json", EXPECTED, EVIDENCE)

    def test_05_rejects_markdown_wrapping(self):
        with self.assertRaises(ReviewOutputError):
            validate_review_output("```json\n" + canonical_json(payload()) + "\n```", EXPECTED, EVIDENCE)

    def test_06_rejects_top_level_extra_key(self):
        obj = payload(); obj["shell_command"] = "git push"
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_07_rejects_missing_top_level_key(self):
        obj = payload(); del obj["confidence"]
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_08_rejects_wrong_review_id(self):
        obj = payload(); obj["review_id"] = "c" * 64
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_09_rejects_wrong_review_state_hash(self):
        obj = payload(); obj["review_state_sha256"] = "d" * 64
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_10_rejects_wrong_lane_or_phase(self):
        obj = payload(); obj["phase"] = "A07"
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_11_rejects_unknown_decision(self):
        obj = payload(); obj["decision"] = "MERGE"
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_12_rejects_missing_top_level_evidence(self):
        obj = payload(); obj["evidence_refs"] = []
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_13_rejects_unknown_evidence_reference(self):
        obj = payload(); obj["evidence_refs"] = ["filesystem:/etc/passwd"]
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_14_rejects_action_without_evidence(self):
        obj = payload(); obj["required_actions"][0]["evidence_refs"] = []
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_15_retry_requires_action(self):
        obj = payload(); obj["required_actions"] = []
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_16_approve_cannot_smuggle_required_actions(self):
        obj = payload("APPROVE"); obj["required_actions"] = payload()["required_actions"]
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_17_rejects_extra_finding_keys(self):
        obj = payload(); obj["findings"][0]["execute"] = True
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_18_rejects_invalid_severity_and_confidence(self):
        obj = payload(); obj["findings"][0]["severity"] = "catastrophic"
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)
        obj = payload(); obj["confidence"] = "certain"
        with self.assertRaises(ReviewOutputError):
            validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)

    def test_19_redacts_secret_material_from_decision(self):
        obj = payload(); obj["reason"] = "token=ghp_" + "x" * 32
        got = validate_review_output(canonical_json(obj), EXPECTED, EVIDENCE)
        self.assertIn("[REDACTED]", got["reason"])
        self.assertNotIn("ghp_", got["reason"])

    def test_20_failure_fingerprint_ignores_evidence_order(self):
        left = payload(); right = deepcopy(left)
        right["evidence_refs"] = ["ci:Build linux"]
        right["findings"][0]["evidence_refs"] = ["ci:Build linux"]
        self.assertEqual(review_failure_fingerprint(left), review_failure_fingerprint(right))

    def test_21_failure_fingerprint_changes_with_repair_guidance(self):
        left = payload(); right = deepcopy(left)
        right["required_actions"][0]["action"] += " Also add a unit test."
        self.assertNotEqual(review_failure_fingerprint(left), review_failure_fingerprint(right))

    def test_22_prompt_states_human_runtime_boundary(self):
        prompt = review_prompt(EXPECTED["review_id"], EVIDENCE, BUNDLE)
        self.assertIn("cannot be advanced by approval", prompt)
        self.assertIn("human decision", prompt)
        self.assertIn("bundle.diff", prompt)
        self.assertIn(canonical_json(BUNDLE), prompt)
        self.assertIn("Do not read files or paths", prompt)
        self.assertNotIn("/tmp/review/bundle.json", prompt)
        self.assertNotIn("git push", prompt)

    def test_23_bubblewrap_configuration_is_read_only_and_credential_isolated(self):
        args = build_bwrap_command("/bundle", "/result", "/codex-home", "/repo", "gpt-6-sol", "max")
        self.assertIn("--sandbox", args)
        self.assertIn("read-only", args)
        self.assertIn("GH_CONFIG_DIR=/home/skyrimdev/.codex/empty-gh", args)
        self.assertIn("--tmpfs", args)
        self.assertIn("/var/lib/skyrim-dev", args)
        self.assertIn("/var/lib/skyrim-review", args)
        self.assertIn("/srv/projects/skyrim-online-str/workers", args)
        self.assertIn("mcp_servers.codex.enabled=false", args)
        self.assertIn('mcp_servers.codex.command="/bin/false"', args)
        self.assertIn("approval_policy=\"never\"", args)
        self.assertNotIn("--full-auto", args)

    def test_24_output_schema_disallows_additional_properties(self):
        self.assertIs(REVIEW_OUTPUT_SCHEMA["additionalProperties"], False)

    def test_25_json_hash_is_stable_under_mapping_order(self):
        self.assertEqual(sha256_json({"a": 1, "b": 2}), sha256_json({"b": 2, "a": 1}))

from architect_review import ArchitectReviewMixin


class DecisionHarness(ArchitectReviewMixin):
    def __init__(self, review_type="CURRENT_PHASE_REVIEW", decision="APPROVE"):
        self.config = {"required_workflows": ["Build linux"]}
        self.state = {
            "global_mode": "RUNNING",
            "control_plane": {"applied_sha": "c" * 40, "status": "VALID"},
            "lanes": {"authority": {
                "state": "NEEDS_SOL_REVIEW", "phase_id": "A06", "phase_title": "authority",
                "worker_attempt": 1, "last_commit": EXPECTED["reviewed_sha"],
                "review": {"type": review_type, "reviewed_phase": {"id": "A06"},
                           "commit_sha": EXPECTED["reviewed_sha"], "next_phase": {"id": "A07"}},
                "ci": {"status": "NOT_RUN", "sha": EXPECTED["reviewed_sha"]},
                "history": [], "success_since_review": 0,
            }},
            "scheduler": {"tasks": {}},
            "architect_review": {"enabled": True, "items": {}, "phase_counters": {}},
        }
        self.processes = {}
        self.active_review_id = None
        self.events = []
        self.approvals = 0
        self.saved = 0
        self.recomputed = 0
        self.item = {
            "review_id": EXPECTED["review_id"], "lane": "authority", "phase": "A06",
            "review_type": review_type, "reviewed_sha": EXPECTED["reviewed_sha"],
            "review_state_sha256": EXPECTED["review_state_sha256"], "worker_attempt": 1,
            "control_plane_sha": "c" * 40, "status": "DECISION_PENDING",
            "decision": {
                "decision": decision, "confidence": "high", "reason": "review result",
                "findings": [], "required_actions": [],
            },
        }
        self.state["architect_review"]["items"][EXPECTED["review_id"]] = self.item

    def _architect_helpers(self):
        import supervisor
        return supervisor

    def refresh_control(self):
        return True

    def _control_plane_valid(self):
        return True

    def _validate_current_review_state(self, item):
        return True, "", {"roadmap_dependencies_and_gates": {"dependencies": {}}}

    def _review_dependencies_satisfied(self, bundle):
        return True, ""

    def _capture_recovery_context(self, lane):
        return {"last_error": "bounded context"}

    def approve_review(self, lane):
        self.approvals += 1
        self.state["lanes"][lane]["state"] = "READY"
        return 0

    def queue_sol_reviews(self):
        return 0

    def event(self, message, lane=None):
        self.events.append((message, lane))

    def _recompute_scheduler(self):
        self.recomputed += 1

    def save_state(self):
        self.saved += 1
        return True


class DecisionPolicyTests(unittest.TestCase):
    def test_26_current_phase_approval_never_advances(self):
        harness = DecisionHarness("CURRENT_PHASE_REVIEW", "APPROVE")
        self.assertFalse(harness._apply_architect_decision(harness.item))
        self.assertEqual(harness.item["status"], "FAILED")
        self.assertEqual(harness.state["lanes"]["authority"]["state"], "NEEDS_SOL_REVIEW")
        self.assertEqual(harness.approvals, 0)

    def test_27_high_severity_retry_with_bounded_actions_stays_repairable(self):
        harness = DecisionHarness("CURRENT_PHASE_REVIEW", "RETRY")
        harness.item["decision"]["findings"] = [{"severity": "critical", "title": "unsafe authority"}]
        harness.item["decision"]["required_actions"] = [{
            "action": "Correct the authority check and run the required exact-SHA workflows.",
            "evidence_refs": ["bundle.diff"],
        }]
        self.assertTrue(harness._apply_architect_decision(harness.item))
        self.assertEqual(harness.item["status"], "APPLIED")
        self.assertEqual(harness.state["lanes"]["authority"]["state"], "RECOVERING")

    def test_27b_high_severity_checkpoint_approval_is_blocked(self):
        harness = DecisionHarness("POST_PHASE_CHECKPOINT", "APPROVE")
        harness.item["decision"]["findings"] = [{"severity": "high", "title": "unresolved identity risk"}]
        harness.state["lanes"]["authority"]["ci"] = {
            "status": "PASS", "sha": EXPECTED["reviewed_sha"],
            "required_workflows": [{"workflow": "Build linux", "status": "completed", "conclusion": "success", "head_sha": EXPECTED["reviewed_sha"]}],
        }
        self.assertTrue(harness._apply_architect_decision(harness.item))
        self.assertEqual(harness.item["status"], "BLOCKED")
        self.assertEqual(harness.approvals, 0)

    def test_27c_retry_without_required_actions_is_blocked(self):
        harness = DecisionHarness("CURRENT_PHASE_REVIEW", "RETRY")
        self.assertTrue(harness._apply_architect_decision(harness.item))
        self.assertEqual(harness.item["status"], "BLOCKED")
        self.assertEqual(harness.state["lanes"]["authority"]["state"], "BLOCKED")

    def test_27d_legacy_actionable_retry_is_revalidated_and_applied(self):
        harness = DecisionHarness("CURRENT_PHASE_REVIEW", "RETRY")
        lane = harness.state["lanes"]["authority"]
        lane["state"] = "BLOCKED"
        lane["review"]["decision"] = "SOL_BLOCKED"
        harness.item["status"] = "BLOCKED"
        harness.item["application_reason"] = LEGACY_SEVERITY_BLOCK_REASON + ": bounded finding"
        harness.item["decision"]["findings"] = [{"severity": "high", "title": "exact CI missing"}]
        harness.item["decision"]["required_actions"] = [{
            "action": "Commit the reviewed diff and pass Linux and Windows exact-SHA CI.",
            "evidence_refs": ["bundle.ci"],
        }]
        self.assertTrue(should_recheck_legacy_blocked_retry(harness.item, lane))
        harness.poll_reviewer()
        self.assertEqual(lane["state"], "RECOVERING")
        self.assertEqual(harness.item["status"], "APPLIED")
        self.assertEqual(harness.item["policy_recheck_version"], 2)

    def test_28_low_confidence_blocks_lane(self):
        harness = DecisionHarness("CURRENT_PHASE_REVIEW", "RETRY")
        harness.item["decision"]["confidence"] = "low"
        self.assertTrue(harness._apply_architect_decision(harness.item))
        self.assertEqual(harness.state["lanes"]["authority"]["state"], "BLOCKED")

    def test_29_checkpoint_approval_requires_exact_sha_ci(self):
        harness = DecisionHarness("POST_PHASE_CHECKPOINT", "APPROVE")
        self.assertTrue(harness._apply_architect_decision(harness.item))
        self.assertEqual(harness.approvals, 0)
        self.assertEqual(harness.state["lanes"]["authority"]["state"], "BLOCKED")

    def test_30_checkpoint_approval_advances_only_after_exact_ci_pass(self):
        harness = DecisionHarness("POST_PHASE_CHECKPOINT", "APPROVE")
        harness.state["lanes"]["authority"]["ci"] = {
            "status": "PASS", "sha": EXPECTED["reviewed_sha"],
            "required_workflows": [{"workflow": "Build linux", "status": "completed", "conclusion": "success", "head_sha": EXPECTED["reviewed_sha"]}],
        }
        self.assertTrue(harness._apply_architect_decision(harness.item))
        self.assertEqual(harness.approvals, 1)
        self.assertEqual(harness.item["status"], "APPLIED")

    def test_31_retry_stays_on_same_phase_and_carries_guidance(self):
        harness = DecisionHarness("CURRENT_PHASE_REVIEW", "RETRY")
        harness.item["decision"]["required_actions"] = [{"action": "Add direct includes and rerun exact required workflows.", "evidence_refs": ["bundle.diff"]}]
        self.assertTrue(harness._apply_architect_decision(harness.item))
        lane = harness.state["lanes"]["authority"]
        self.assertEqual(lane["state"], "RECOVERING")
        self.assertEqual(lane["phase_id"], "A06")
        self.assertIn("architect_review", lane["recovery_context"])
        self.assertEqual(harness.item["status"], "APPLIED")

    def test_32_stale_review_decision_is_not_applied(self):
        harness = DecisionHarness("POST_PHASE_CHECKPOINT", "APPROVE")
        harness._validate_current_review_state = lambda item: (False, "head changed", None)
        self.assertFalse(harness._apply_architect_decision(harness.item))
        self.assertEqual(harness.item["status"], "STALE")
        self.assertEqual(harness.approvals, 0)

    def test_33_paused_global_mode_defers_decision_application(self):
        harness = DecisionHarness("POST_PHASE_CHECKPOINT", "APPROVE")
        harness.state["global_mode"] = "PAUSED"
        self.assertFalse(harness._apply_architect_decision(harness.item))
        self.assertEqual(harness.approvals, 0)
        self.assertNotEqual(harness.item["status"], "APPLIED")



class ReviewQueueStabilityTests(unittest.TestCase):
    def test_40_normalization_drops_only_nested_evaluation_timestamps(self):
        value = {
            "task": {"state": "DONE", "evaluated_at": "first"},
            "dependencies": {"C04": {"state": "DONE", "evaluated_at": "second"}},
            "other_timestamp": "preserved",
        }
        got = normalize_review_evidence(value)
        self.assertEqual(got, {
            "task": {"state": "DONE"},
            "dependencies": {"C04": {"state": "DONE"}},
            "other_timestamp": "preserved",
        })
        self.assertEqual(sha256_json(got), sha256_json(normalize_review_evidence(value)))

    def test_digest_ignores_evaluation_timestamps_across_roadmap_evidence(self):
        first = {
            "roadmap_dependencies_and_gates": {
                "task": {"state": "DONE", "evaluated_at": "tick-1"},
                "dependencies": {"C04": {"state": "DONE", "evaluated_at": "tick-1"}},
            },
            "head": "abc123",
        }
        second = {
            "roadmap_dependencies_and_gates": {
                "task": {"state": "DONE", "evaluated_at": "tick-2"},
                "dependencies": {"C04": {"state": "DONE", "evaluated_at": "tick-2"}},
            },
            "head": "abc123",
        }
        self.assertEqual(stable_review_state_sha256(first), stable_review_state_sha256(second))
        second["roadmap_dependencies_and_gates"]["dependencies"]["C04"]["state"] = "FAILED"
        self.assertNotEqual(stable_review_state_sha256(first), stable_review_state_sha256(second))

    def test_review_identity_and_bundle_ignore_evaluation_ticks(self):
        first_state = {"roadmap": {"task": {"state": "DONE", "evaluated_at": "tick-1"}}}
        second_state = {"roadmap": {"task": {"state": "DONE", "evaluated_at": "tick-2"}}}
        first_sha = stable_review_state_sha256(first_state)
        second_sha = stable_review_state_sha256(second_state)
        self.assertEqual(first_sha, second_sha)
        args = ("combat", "C05", "CURRENT_PHASE_REVIEW", "a" * 40)
        self.assertEqual(review_identity(*args, first_sha), review_identity(*args, second_sha))
        first_bundle = normalize_review_evidence({"roadmap": first_state["roadmap"], "review_id": "stable"})
        second_bundle = normalize_review_evidence({"roadmap": second_state["roadmap"], "review_id": "stable"})
        self.assertEqual(first_bundle, second_bundle)
        self.assertNotIn("evaluated_at", first_bundle["roadmap"]["task"])

    def test_41_queue_coalesces_superseded_items_and_keeps_other_lanes(self):
        queue = ["old", "authority", "old", "terminal"]
        items = {
            "old": {"lane": "combat", "status": "QUEUED"},
            "authority": {"lane": "authority", "status": "QUEUED"},
            "terminal": {"lane": "population", "status": "STALE"},
        }
        changed = coalesce_queued_review_items(queue, items, "combat", "current")
        self.assertTrue(changed)
        self.assertEqual(queue, ["authority"])
        self.assertEqual(items["old"]["status"], "STALE")
        self.assertIn("newest exact-state", items["old"]["last_error"])

    def test_42_queue_coalescing_is_idempotent_for_current_item(self):
        queue = ["current", "authority"]
        items = {
            "current": {"lane": "combat", "status": "QUEUED"},
            "authority": {"lane": "authority", "status": "QUEUED"},
        }
        changed = coalesce_queued_review_items(queue, items, "combat", "current")
        self.assertFalse(changed)
        self.assertEqual(queue, ["current", "authority"])


class ToolFreeInvocationTests(unittest.TestCase):
    def test_43_accepts_plain_model_response_events(self):
        events = [
            {"type": "thread.started"},
            {"type": "turn.started"},
            {"type": "item.completed", "item": {"type": "agent_message", "text": "decision"}},
            {"type": "turn.completed"},
        ]
        validate_tool_free_jsonl("\n".join(canonical_json(event) for event in events))

    def test_44_rejects_mcp_tool_events(self):
        events = [
            {"type": "item.started", "item": {"type": "mcp_tool_call", "server": "codex", "tool": "list_mcp_resources"}},
            {"type": "item.completed", "item": {"type": "agent_message", "text": "done"}},
        ]
        with self.assertRaises(ReviewOutputError):
            validate_tool_free_jsonl("\n".join(canonical_json(event) for event in events))

    def test_45_rejects_command_execution_events(self):
        events = [
            {"type": "item.completed", "item": {"type": "command_execution", "aggregated_output": "ok"}},
            {"type": "item.completed", "item": {"type": "agent_message", "text": "done"}},
        ]
        with self.assertRaises(ReviewOutputError):
            validate_tool_free_jsonl("\n".join(canonical_json(event) for event in events))

    def test_46_rejects_unknown_non_json_event_data(self):
        with self.assertRaises(ReviewOutputError):
            validate_tool_free_jsonl("{" + "\n" + "plain warning text")

    def test_47_rejects_bundle_identity_mismatch(self):
        wrong = {**BUNDLE, "review_id": "c" * 64}
        with self.assertRaises(ReviewOutputError):
            review_prompt(EXPECTED["review_id"], EVIDENCE, wrong)

    def test_48_rejects_prompt_bundle_evidence_mismatch(self):
        wrong = {**BUNDLE, "evidence_ids": ["unknown.ref"]}
        with self.assertRaises(ReviewOutputError):
            review_prompt(EXPECTED["review_id"], EVIDENCE, wrong)


if __name__ == "__main__":
    unittest.main()
