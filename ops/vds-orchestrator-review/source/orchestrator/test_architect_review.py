from __future__ import annotations

import json
import unittest
from copy import deepcopy

from architect_review import (
    REVIEW_OUTPUT_SCHEMA,
    ReviewOutputError,
    ReviewEvidenceError,
    ArchitectReviewMixin,
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
        self.assertIn("committed_phase_diff", prompt)
        self.assertIn("worktree_evidence", prompt)
        self.assertIn("uncommitted state", prompt)
        self.assertIn('for APPROVE, set required_actions to an empty array exactly: []', prompt)
        self.assertIn('choose RETRY and list concrete, bounded actions', prompt)
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

    def test_review_identity_is_version_three_and_cannot_reuse_legacy_v2_id(self):
        args = ("population", "L05", "POST_PHASE_CHECKPOINT", "a" * 40, "b" * 64)
        legacy = sha256_json({"evidence_version": 2, "lane": args[0], "phase": args[1],
                              "review_type": args[2], "reviewed_sha": args[3],
                              "review_state_sha256": args[4]})
        self.assertNotEqual(review_identity(*args), legacy)
        self.assertEqual(review_identity(*args), sha256_json({
            "evidence_version": 3, "lane": args[0], "phase": args[1],
            "review_type": args[2], "reviewed_sha": args[3],
            "review_state_sha256": args[4],
        }))

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


class EvidenceGitHarness(ArchitectReviewMixin):
    def __init__(self, worktree):
        self.worktree = str(worktree)
        self.config = {
            "architect_review": {"max_context_files": 32, "max_context_bytes": 1024 * 1024,
                                 "max_committed_diff_bytes": 512 * 1024},
            "phase_completion": {},
        }

    def command(self, args, cwd=None, timeout=90):
        import subprocess
        proc = subprocess.run(args, cwd=cwd, capture_output=True, timeout=timeout)
        return proc.returncode, proc.stdout.decode("utf-8", errors="replace"), proc.stderr.decode("utf-8", errors="replace")


class CommittedPhaseEvidenceTests(unittest.TestCase):
    def setUp(self):
        import tempfile
        import subprocess
        from pathlib import Path
        self.temp = tempfile.TemporaryDirectory(prefix="review-evidence-")
        self.root = Path(self.temp.name)
        self.git(["init", "-q", "-b", "main"])
        self.git(["config", "user.email", "test@example.invalid"])
        self.git(["config", "user.name", "Evidence Test"])
        (self.root / "src").mkdir()
        (self.root / "docs").mkdir()
        (self.root / "src" / "engine.py").write_text("def run():\n    return 'old'\n", encoding="utf-8")
        self.git(["add", "."])
        self.git(["commit", "-q", "-m", "accepted boundary"])
        self.base = self.git(["rev-parse", "HEAD"]).strip()
        (self.root / "src" / "engine.py").write_text("def run():\n    return 'new'\n", encoding="utf-8")
        (self.root / "docs" / "spec.md").write_text("phase implementation evidence\n", encoding="utf-8")
        self.git(["add", "."])
        self.git(["commit", "-q", "-m", "phase implementation"])
        self.head = self.git(["rev-parse", "HEAD"]).strip()
        self.harness = EvidenceGitHarness(self.root)
        self.lane = {"history": [
            {"phase": "P00", "commit": self.base},
            {"phase": "P01", "commit": self.head},
        ]}

    def tearDown(self):
        self.temp.cleanup()

    def git(self, args):
        import subprocess
        proc = subprocess.run(["git", "-C", str(self.root), *args], capture_output=True, check=True)
        return proc.stdout.decode("utf-8", errors="replace")

    def evidence(self, review_type="POST_PHASE_CHECKPOINT", lane=None, phase="P01", head=None):
        return self.harness._committed_phase_evidence(
            "authority", lane or self.lane, review_type, phase,
            str(self.root), head or self.head,
        )

    def test_01_uses_last_different_accepted_phase_boundary(self):
        lane = {"history": [{"phase": "P00", "commit": self.base}, {"phase": "P01", "commit": self.head}]}
        self.assertEqual(self.harness._trusted_previous_accepted_head(lane, "P01"), self.base)

    def test_02_ignores_malformed_history_entries(self):
        lane = {"history": [None, {"phase": "P00", "commit": "bad"}, {"phase": "P00", "commit": self.base}]}
        self.assertEqual(self.harness._trusted_previous_accepted_head(lane, "P01"), self.base)

    def test_03_missing_trusted_base_is_evidence_error(self):
        with self.assertRaises(ReviewEvidenceError) as caught:
            self.evidence(lane={"history": []})
        self.assertEqual(caught.exception.code, "TRUSTED_BASE_MISSING")

    def test_04_missing_base_commit_is_evidence_error(self):
        lane = {"history": [{"phase": "P00", "commit": "1" * 40}]}
        with self.assertRaises(ReviewEvidenceError) as caught:
            self.evidence(lane=lane)
        self.assertEqual(caught.exception.code, "REVIEW_COMMIT_MISSING")

    def test_05_missing_reviewed_commit_is_evidence_error(self):
        with self.assertRaises(ReviewEvidenceError) as caught:
            self.evidence(head="2" * 40)
        self.assertEqual(caught.exception.code, "REVIEW_COMMIT_MISSING")

    def test_06_non_ancestor_base_is_rejected(self):
        self.git(["checkout", "-q", "--orphan", "unrelated"])
        (self.root / "orphan.txt").write_text("unrelated\n", encoding="utf-8")
        self.git(["add", "."])
        self.git(["commit", "-q", "-m", "unrelated root"])
        orphan = self.git(["rev-parse", "HEAD"]).strip()
        with self.assertRaises(ReviewEvidenceError) as caught:
            self.evidence(head=orphan)
        self.assertEqual(caught.exception.code, "TRUSTED_BASE_NOT_ANCESTOR")

    def test_07_diff_contains_committed_change(self):
        evidence = self.evidence()
        self.assertIn("return 'new'", evidence["committed_phase_diff"])
        self.assertIn("-    return 'old'", evidence["committed_phase_diff"])

    def test_08_diff_sha256_matches_exact_git_bytes(self):
        import hashlib
        import subprocess
        evidence = self.evidence()
        raw = subprocess.run([
            "git", "-C", str(self.root), "diff", "--no-ext-diff", "--binary",
            "--find-renames", "--unified=3", f"{self.base}..{self.head}",
        ], stdout=subprocess.PIPE, check=True).stdout
        self.assertEqual(evidence["committed_phase_diff_sha256"], hashlib.sha256(raw).hexdigest())

    def test_09_dirty_worktree_does_not_change_committed_diff(self):
        import hashlib
        before = self.evidence()
        (self.root / "src" / "engine.py").write_text("uncommitted unrelated change\n", encoding="utf-8")
        after = self.evidence()
        self.assertEqual(before["committed_phase_diff_sha256"], after["committed_phase_diff_sha256"])
        self.assertEqual(before["committed_phase_diff"], after["committed_phase_diff"])

    def test_10_inventory_includes_added_path(self):
        paths = {row["path"] for row in self.evidence()["committed_phase_files"]}
        self.assertIn("docs/spec.md", paths)

    def test_11_source_content_comes_from_reviewed_sha_not_worktree(self):
        (self.root / "src" / "engine.py").write_text("dirty worktree content\n", encoding="utf-8")
        evidence = self.evidence()
        source = self.harness._committed_source_context(str(self.root), self.head, evidence["committed_phase_files"])
        engine = next(row for row in source if row["path"] == "src/engine.py")
        self.assertIn("return 'new'", engine["reviewed_sha_content"])
        self.assertNotIn("dirty worktree", engine["reviewed_sha_content"])

    def test_12_source_sha256_is_blob_content_hash(self):
        import hashlib
        evidence = self.evidence()
        source = self.harness._committed_source_context(str(self.root), self.head, evidence["committed_phase_files"])
        engine = next(row for row in source if row["path"] == "src/engine.py")
        expected = hashlib.sha256(b"def run():\n    return 'new'\n").hexdigest()
        self.assertEqual(engine["content_sha256"], expected)

    def test_13_multiple_phase_commits_are_listed_in_order(self):
        (self.root / "docs" / "second.md").write_text("second commit\n", encoding="utf-8")
        self.git(["add", "."])
        self.git(["commit", "-q", "-m", "second phase commit"])
        head = self.git(["rev-parse", "HEAD"]).strip()
        evidence = self.evidence(head=head)
        self.assertEqual(len(evidence["phase_commits"]), 2)
        self.assertEqual(evidence["phase_commits"][-1]["sha"], head)
        self.assertEqual(evidence["phase_commits"][-1]["message"], "second phase commit")

    def test_14_file_stat_is_present(self):
        self.assertIn("engine.py", self.evidence()["committed_phase_stat"])

    def test_15_rename_inventory_records_old_and_new_path(self):
        self.git(["checkout", "-q", self.base])
        (self.root / "docs").mkdir(exist_ok=True)
        (self.root / "docs" / "move.md").write_text("stable contents for rename detection\n", encoding="utf-8")
        self.git(["add", "."])
        self.git(["commit", "-q", "-m", "add rename source at accepted boundary"])
        base = self.git(["rev-parse", "HEAD"]).strip()
        self.git(["mv", "docs/move.md", "docs/renamed-move.md"])
        self.git(["commit", "-q", "-m", "rename phase document"])
        head = self.git(["rev-parse", "HEAD"]).strip()
        evidence = self.evidence(head=head, lane={"history": [{"phase": "P00", "commit": base}]})
        rename = next(row for row in evidence["committed_phase_files"] if row["change_type"].startswith("R"))
        self.assertEqual(rename["original_path"], "docs/move.md")
        self.assertEqual(rename["path"], "docs/renamed-move.md")

    def test_16_deleted_path_is_in_inventory(self):
        self.git(["checkout", "-q", self.base])
        (self.root / "docs").mkdir(exist_ok=True)
        (self.root / "docs" / "delete.md").write_text("file to delete in phase\n", encoding="utf-8")
        self.git(["add", "."])
        self.git(["commit", "-q", "-m", "add delete source at accepted boundary"])
        base = self.git(["rev-parse", "HEAD"]).strip()
        self.git(["rm", "docs/delete.md"])
        self.git(["commit", "-q", "-m", "delete phase document"])
        head = self.git(["rev-parse", "HEAD"]).strip()
        evidence = self.evidence(head=head, lane={"history": [{"phase": "P00", "commit": base}]})
        deleted = next(row for row in evidence["committed_phase_files"] if row["change_type"] == "D")
        self.assertEqual(deleted["path"], "docs/delete.md")

    def test_17_deleted_source_context_states_contents_are_in_diff(self):
        self.git(["checkout", "-q", self.base])
        (self.root / "docs").mkdir(exist_ok=True)
        (self.root / "docs" / "delete.md").write_text("file to delete in phase\n", encoding="utf-8")
        self.git(["add", "."])
        self.git(["commit", "-q", "-m", "add delete source at accepted boundary"])
        base = self.git(["rev-parse", "HEAD"]).strip()
        self.git(["rm", "docs/delete.md"])
        self.git(["commit", "-q", "-m", "delete phase document"])
        head = self.git(["rev-parse", "HEAD"]).strip()
        evidence = self.evidence(head=head, lane={"history": [{"phase": "P00", "commit": base}]})
        source = self.harness._committed_source_context(str(self.root), head, evidence["committed_phase_files"])
        deleted = next(row for row in source if row["change_type"] == "D")
        self.assertEqual(deleted["content_status"], "deleted_at_reviewed_sha")

    def test_18_empty_checkpoint_range_fails_closed(self):
        lane = {"history": [{"phase": "P00", "commit": self.head}]}
        with self.assertRaises(ReviewEvidenceError) as caught:
            self.evidence(lane=lane, head=self.head)
        self.assertEqual(caught.exception.code, "EMPTY_CHECKPOINT_RANGE")

    def test_19_evidence_only_checkpoint_can_have_empty_range(self):
        self.harness.config["phase_completion"]["P01"] = {"completion_mode": "evidence-only"}
        lane = {"history": [{"phase": "P00", "commit": self.head}]}
        evidence = self.evidence(lane=lane, head=self.head)
        self.assertEqual(evidence["committed_phase_files"], [])

    def test_20_current_phase_review_can_have_empty_committed_range(self):
        lane = {"history": [{"phase": "P00", "commit": self.head}]}
        evidence = self.evidence("CURRENT_PHASE_REVIEW", lane=lane, head=self.head)
        self.assertEqual(evidence["committed_phase_diff"], "")

    def test_21_committed_diff_byte_bound_fails_closed(self):
        self.harness.config["architect_review"]["max_committed_diff_bytes"] = 8
        with self.assertRaises(ReviewEvidenceError) as caught:
            self.evidence()
        self.assertEqual(caught.exception.code, "COMMITTED_DIFF_BOUND_EXCEEDED")

    def test_22_unsafe_git_path_is_rejected(self):
        with self.assertRaises(ReviewEvidenceError) as caught:
            self.harness._reviewed_blob(str(self.root), self.head, "../outside", 1024)
        self.assertEqual(caught.exception.code, "UNSAFE_REVIEW_PATH")

    def test_23_source_context_tail_still_has_exact_sha_hash(self):
        import hashlib
        evidence = self.evidence()
        self.harness.config["architect_review"]["max_context_files"] = 1
        source = self.harness._committed_source_context(str(self.root), self.head, evidence["committed_phase_files"])
        tail = source[1]
        self.assertEqual(tail["content_status"], "omitted_too_large")
        self.assertEqual(tail["content_sha256"], hashlib.sha256(b"def run():\n    return 'new'\n").hexdigest())
        self.assertEqual(tail["reviewed_sha"], self.head)

    def test_24_binary_source_is_hashed_but_not_decoded(self):
        (self.root / "image.bin").write_bytes(b"\x00\xffbinary")
        self.git(["add", "."])
        self.git(["commit", "-q", "-m", "add binary evidence"])
        head = self.git(["rev-parse", "HEAD"]).strip()
        blob = self.harness._reviewed_blob(str(self.root), head, "image.bin", 1024)
        self.assertEqual(blob["content_status"], "omitted_binary_or_non_utf8")
        self.assertIsNotNone(blob["content_sha256"])
        self.assertIsNone(blob["reviewed_sha_content"])

    def test_25_context_limit_keeps_hash_for_each_changed_path(self):
        evidence = self.evidence()
        self.harness.config["architect_review"]["max_context_files"] = 1
        source = self.harness._committed_source_context(str(self.root), self.head, evidence["committed_phase_files"])
        self.assertEqual(len(source), 2)
        self.assertTrue(all(row.get("content_sha256") for row in source))

    def test_26_symlink_blob_is_not_followed(self):
        (self.root / "link.md").symlink_to("src/engine.py")
        self.git(["add", "link.md"])
        self.git(["commit", "-q", "-m", "add symlink evidence"])
        head = self.git(["rev-parse", "HEAD"]).strip()
        blob = self.harness._reviewed_blob(str(self.root), head, "link.md", 1024)
        self.assertEqual(blob["content_status"], "omitted_symlink")
        self.assertIsNone(blob["content_sha256"])

    def test_27_oversize_git_object_is_rejected_before_blob_read(self):
        class LargeBlobHarness(EvidenceGitHarness):
            def command(self, args, cwd=None, timeout=90):
                if len(args) >= 6 and args[-2:-1] == ["-s"]:
                    return 0, str(128 * 1024 * 1024 + 1), ""
                return super().command(args, cwd, timeout)
        harness = LargeBlobHarness(self.root)
        with self.assertRaises(ReviewEvidenceError) as caught:
            harness._reviewed_blob(str(self.root), self.head, "src/engine.py", 1024)
        self.assertEqual(caught.exception.code, "GIT_OBJECT_HASH_BOUND_EXCEEDED")


class QueueEvidenceHarness(ArchitectReviewMixin):
    def __init__(self, failed_lane="combat"):
        self.failed_lane = failed_lane
        self.config = {"architect_review": {"deterministic_evidence_error_max_attempts": 3,
                                             "evidence_error_backoff_seconds": 60}}
        self.state = {
            "global_mode": "PAUSED",
            "control_plane": {"applied_sha": "c" * 40, "status": "VALID"},
            "lanes": {},
            "architect_review": {"enabled": True, "queue": [], "items": {},
                                  "phase_counters": {}, "evidence_errors": {}},
        }
        for lane, phase in (("combat", "C05"), ("authority", "A09"), ("population", "L05"), ("ui", "U04")):
            sha = (lane[0] * 40)
            self.state["lanes"][lane] = {
                "state": "NEEDS_SOL_REVIEW", "phase_id": phase,
                "phase_title": phase, "worker_attempt": 1,
                "review": {"type": "CURRENT_PHASE_REVIEW", "reviewed_phase": {"id": phase}, "commit_sha": sha},
                "last_commit": sha, "history": [], "ci": {"status": "PASS", "sha": sha},
            }
        self.events = []

    def _control_plane_valid(self):
        return True

    def build_review_bundle(self, lane_name):
        from architect_review import sha256_json
        if lane_name == self.failed_lane:
            raise ReviewEvidenceError("EXACT_SHA_CI_MISSING", "exact CI evidence is unavailable")
        lane = self.state["lanes"][lane_name]
        phase = lane["phase_id"]
        sha = lane["last_commit"]
        review_id = sha256_json({"lane": lane_name, "phase": phase, "sha": sha, "v": 3})
        return {"review_id": review_id, "review_state_sha256": "d" * 64,
                "evidence_version": 3, "bundle_schema_version": 3,
                "lane": lane_name, "phase": phase, "review_type": lane["review"]["type"],
                "reviewed_sha": sha, "worker_attempt": 1,
                "control_plane_sha": "c" * 40, "bundle_dir": "/bundle",
                "bundle_path": f"/bundle/{review_id}.json"}

    def event(self, message, lane=None):
        self.events.append((message, lane))

    def save_state(self):
        return True


class V3ContinuationHarness(QueueEvidenceHarness):
    def __init__(self):
        super().__init__(failed_lane="__none__")
        phases = {"combat": "C05", "authority": "A09", "population": "L05", "ui": "U04"}
        shas = {"combat": "a" * 40, "authority": "b" * 40, "population": "c" * 40, "ui": "d" * 40}
        self.target_phases = phases
        self.target_shas = shas
        self.state["architect_review"].update({"queue": [], "items": {}, "operator_v3_targets": None,
                                                "read_only_while_paused": False})
        for lane_name in phases:
            lane = self.state["lanes"][lane_name]
            lane.update({"worktree": lane_name, "last_commit": shas[lane_name],
                         "review": {"type": "POST_PHASE_CHECKPOINT",
                                    "reviewed_phase": {"id": phases[lane_name]},
                                    "commit_sha": shas[lane_name], "decision": "SOL_BLOCKED"}})
        combat = self.state["lanes"]["combat"]
        combat.update({"state": "PAUSED", "phase_id": "C06", "phase_index": 5,
                       "review": {"type": "POST_PHASE_CHECKPOINT", "reviewed_phase": {"id": "C06"},
                                  "commit_sha": "e" * 40, "decision": "APPROVED_ADVANCE_ONCE"}})
        self.state["architect_review"]["items"]["combat-applied"] = {
            "lane": "combat", "phase": "C05", "reviewed_sha": shas["combat"],
            "evidence_version": 3, "status": "APPLIED", "decision": {"decision": "APPROVE"},
        }
        self.state["architect_review"]["items"]["authority-stale"] = {
            "lane": "authority", "phase": "A09", "reviewed_sha": shas["authority"],
            "evidence_version": 3, "status": "STALE", "decision": {"decision": "RETRY"},
        }
        self.state["architect_review"]["items"]["population-queued"] = {
            "lane": "population", "phase": "L05", "reviewed_sha": shas["population"],
            "evidence_version": 3, "status": "QUEUED", "decision": None,
        }
        self.state["architect_review"]["items"]["ui-queued"] = {
            "lane": "ui", "phase": "U04", "reviewed_sha": shas["ui"],
            "evidence_version": 3, "status": "QUEUED", "decision": None,
        }
        self.state["architect_review"]["queue"] = ["population-queued", "ui-queued"]
        self.processes = {}
        self.review_process = None
        self.active_review_id = None

    def git_head(self, worktree):
        lane = self.state["lanes"].get(str(worktree), {})
        return lane.get("last_commit")

class EvidenceQueueIsolationTests(unittest.TestCase):
    def test_queue_propagates_operator_authorization_to_rebuilt_v3_items(self):
        h = QueueEvidenceHarness()
        h.state["architect_review"]["operator_v3_targets"] = [
            {"lane": lane, "phase": data["phase_id"], "sha": data["last_commit"]}
            for lane, data in h.state["lanes"].items()
        ]
        h.queue_sol_reviews()
        items = h.state["architect_review"]["items"]
        self.assertTrue(all(item.get("operator_authorized_recheck") for item in items.values()))
        self.assertEqual(len(h.state["architect_review"]["queue"]), 3)

    def test_continuation_restores_readonly_authorization_without_replaying_applied_lane(self):
        h = V3ContinuationHarness()
        targets = [
            {"lane": lane, "phase": h.target_phases[lane], "sha": h.target_shas[lane]}
            for lane in ("combat", "authority", "population", "ui")
        ]
        result = h.request_fresh_reviews_v3(targets)
        review = h.state["architect_review"]
        self.assertEqual(result, 0)
        self.assertTrue(review["read_only_while_paused"])
        self.assertEqual(len(review["operator_v3_targets"]), 4)
        self.assertEqual(h.state["lanes"]["combat"]["phase_id"], "C06")
        self.assertEqual(review["items"]["combat-applied"]["status"], "APPLIED")
        self.assertEqual(review["items"]["combat-applied"]["decision"]["decision"], "APPROVE")
        for lane in ("authority", "population", "ui"):
            queued = [item for item in review["items"].values()
                      if item.get("lane") == lane and item.get("phase") == h.target_phases[lane]
                      and item.get("reviewed_sha") == h.target_shas[lane] and item.get("status") == "QUEUED"]
            self.assertEqual(len(queued), 1)
            self.assertTrue(queued[0].get("operator_authorized_recheck"))

    def test_continuation_fails_closed_on_target_phase_drift(self):
        h = V3ContinuationHarness()
        h.state["lanes"]["authority"]["phase_id"] = "A10"
        targets = [
            {"lane": lane, "phase": h.target_phases[lane], "sha": h.target_shas[lane]}
            for lane in ("combat", "authority", "population", "ui")
        ]
        result = h.request_fresh_reviews_v3(targets)
        self.assertEqual(result, 1)
        self.assertFalse(h.state["architect_review"].get("read_only_while_paused"))

    def test_legacy_block_match_ignores_stale_items_without_decisions(self):
        stale = {"lane": "ui", "phase": "U04", "reviewed_sha": "a" * 40,
                 "status": "STALE", "decision": None}
        blocked = {"lane": "ui", "phase": "U04", "reviewed_sha": "a" * 40,
                   "status": "BLOCKED", "decision": {"decision": "BLOCK"}}
        self.assertFalse(ArchitectReviewMixin._is_matching_legacy_block(stale, "ui", "U04", "a" * 40))
        self.assertTrue(ArchitectReviewMixin._is_matching_legacy_block(blocked, "ui", "U04", "a" * 40))

    def test_failed_lane_preflight_is_recorded_without_blocking_other_lanes(self):
        h = QueueEvidenceHarness()
        queued = h.queue_sol_reviews()
        review = h.state["architect_review"]
        self.assertEqual(queued, 3)
        self.assertEqual(len(review["queue"]), 3)
        self.assertEqual(set(review["items"][rid]["lane"] for rid in review["queue"]), {"authority", "population", "ui"})
        self.assertEqual(len(review["evidence_errors"]), 1)
        error = next(iter(review["evidence_errors"].values()))
        self.assertEqual(error["status"], "EVIDENCE_ERROR")
        self.assertEqual(error["error_code"], "EXACT_SHA_CI_MISSING")
        self.assertEqual(h.state["lanes"]["combat"]["state"], "NEEDS_SOL_REVIEW")

    def test_queued_v3_items_are_idempotent_after_retry_tick(self):
        h = QueueEvidenceHarness()
        self.assertEqual(h.queue_sol_reviews(), 3)
        self.assertEqual(h.queue_sol_reviews(), 0)
        self.assertEqual(len(h.state["architect_review"]["queue"]), 3)
        self.assertEqual(len(h.state["architect_review"]["items"]), 3)


class ReviewDecisionDrainHarness(ArchitectReviewMixin):
    def __init__(self, leave_pending=False):
        self.state = {
            "global_mode": "RUNNING",
            "architect_review": {
                "active_review_id": "finished-review",
                "queue": ["next-review"],
                "items": {"pending-review": {"status": "DECISION_PENDING", "decision": {"decision": "RETRY"}}},
            },
        }
        self.active_review_id = "finished-review"
        self.review_process = object()
        self.processes = {}
        self.leave_pending = leave_pending
        self.events = []

    def poll_reviewer(self):
        if self.review_process is not None:
            self.events.append("finish-review")
            self.review_process = None
            self.active_review_id = None
            self.state["architect_review"]["active_review_id"] = None
            return
        pending = self.state["architect_review"]["items"]["pending-review"]
        if pending["status"] == "DECISION_PENDING":
            if self.leave_pending:
                self.events.append("decision-still-pending")
            else:
                self.events.append("apply-decision")
                pending["status"] = "APPLIED"

    def _control_plane_valid(self):
        return True

    def queue_sol_reviews(self):
        self.events.append("queue-reviews")
        return 0

    def start_next_reviewer(self):
        self.events.append("start-next-reviewer")
        self.active_review_id = "next-review"
        self.state["architect_review"]["active_review_id"] = "next-review"
        return True

    def scheduler_idle_summary(self):
        self.events.append("idle-summary")
        return {"idle_reason": "reviewer active", "runnable": [], "queued_reviews": 0}


class ReviewDecisionSchedulingTests(unittest.TestCase):
    def test_pending_decision_is_applied_before_starting_next_reviewer(self):
        h = ReviewDecisionDrainHarness()
        h.architect_review_tick()
        self.assertLess(h.events.index("apply-decision"), h.events.index("start-next-reviewer"))
        self.assertEqual(h.state["architect_review"]["items"]["pending-review"]["status"], "APPLIED")

    def test_unapplied_decision_blocks_next_reviewer_admission(self):
        h = ReviewDecisionDrainHarness(leave_pending=True)
        h.architect_review_tick()
        self.assertIn("decision-still-pending", h.events)
        self.assertNotIn("start-next-reviewer", h.events)
        self.assertEqual(h.state["architect_review"]["items"]["pending-review"]["status"], "DECISION_PENDING")
