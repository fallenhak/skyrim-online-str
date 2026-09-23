## V3.3 exact-evidence repair and resumed development — 2026-09-23 11:28:40Z

The evidence-pipeline root cause is confirmed: the canonical builder used only dirty/staged/untracked diffs for committed checkpoint review. Clean committed phases therefore reached Sol without committed code. V3.3 now constructs immutable evidence from the trusted accepted base through exact reviewed HEAD, validates ancestry, and supplies raw diff hash, file inventory, all phase commits, exact-SHA Git-object hashes/context, exact-SHA CI, and roadmap/product state. Dirty-worktree evidence remains separate. Evidence-construction faults use a bounded infrastructure error path. The scheduler no longer presents a terminal Sol BLOCK as READY.

Population L05 was incorrectly left CURRENT_PHASE_REVIEW because the clean worktree made the old dirty-diff check appear structurally empty. Its four-commit range and normal phase-completion invariants were checked; the ordinary completion/checkpoint path moved it to POST_PHASE_CHECKPOINT without advancing or approving it.

| Fresh v3 review | Decision | Normal policy result |
| --- | --- | --- |
| Combat C05, 8007bfa4625bd611458cd714241c1ea69dad9abf | APPROVE, high | Advanced once to C06 |
| Authority A09, c9fae7f73f65d813c3fe3a4284caad71fada53a9 | RETRY, high | Same-phase recovery 1/3; equipment authority and sender-range fixes required |
| Population L05, 74d992001cfc63a7bb16ee7418f11bf05de07148 | RETRY, high | Same-phase recovery 2/3; 12-bit light-ID and MAST-slot overflow fixes required |
| UI U04, 6d32bdab944a904098c0c91953fe6d3c0b641753 | APPROVE, medium | Advanced once to U05 |

All four decisions were validated and applied from current evidence-v3 bundles. Old v2 decisions remain immutable. Stale intermediate V3 results were not applied. A malformed UI APPROVE with required actions was rejected; the 900-second backoff was honored. After clarifying the prompt contract, the second attempt returned a valid APPROVE with no required actions. No approval gate was weakened.

Canonical and installed deterministic suites pass 187 tests. Python compile, SELF_TEST_OK, five health checks, WORKER_SMOKE_OK, and isolated gpt-6-sol/max architect-review smoke all pass. Canonical and installed mapped-file SHA-256 values match.

At capture, GLOBAL is RUNNING; orchestrator service and healthcheck timer are active/enabled; the control plane remains VALID at 3e7e893b4018b488e158aa5cda977399c0e55a75. Two gpt-6-luna/max workers occupy the cap of two: Combat C06 PID 403175 and Authority A09 recovery PID 404453. Population L05 HEAD 9c665e0f0ac21fced7cbcf567904328e75aca942 and UI U05 HEAD 45b6eb4f6f0bcdcc105f1f048cf09d239716a53 are waiting for exact-SHA CI. Sol active/queued: 0/0; evidence errors: 0. W01-W10 remain externally gated. M01 is ACTIVE, runtime acceptance remains human-owned, and no M02+ task ran. No development branch was merged or force-pushed, and no destructive Git operation or CI bypass occurred. The Windows PC was left running.

The detailed lane diff, commit, CI, decision, live worker, scheduler, and backup evidence is in [the V3.3 final report](evidence-v3-20260923/final-report.md) and [redacted state projection](evidence-v3-20260923/final-state-redacted.json).

---

## Autonomous Sol deployment and overnight idle state -- 2026-09-23T08:44:28+00:00

Implementation commit 323033ceccb99874e2767531abbb94791243d24d is included in normal
infra-branch merge commit 889199056594c246286a5fd93cba942443996575. That merge was pushed
normally to origin/infra/vds-orchestrator-review. Changed snapshot paths include source/orchestrator/supervisor.py, architect_review.py, test_architect_review.py and test_supervisor.py; source/config/supervisor.json; source/management/skyrim-dev; plus verification and deployment documentation. The upstream merge also preserved the bounded-concurrency repair and its regression tests.
The deployed source snapshot follows
the source map; canonical and installed runtime hashes match.

### Verification

| Check | Result |
| --- | --- |
| Python compile checks | PASS |
| Full deterministic supervisor, roadmap, and reviewer suite | 149 passed |
| Installed runtime self-test | SELF_TEST_OK |
| Installed runtime healthcheck | All five checks passed |
| Worker smoke | WORKER_SMOKE_OK; inbox blocked; no branch/worktree change or persistent smoke worker |
| Architect-review smoke | PASS; gpt-6-sol/max returned strict RETRY JSON with no tool events |
| Infrastructure diff check | PASS |

Canonical and installed SHA-256 values match:

| File | SHA-256 |
| --- | --- |
| supervisor.py | d6fe564e339d5b8832b9ddb26867f1511c3212d85dfb3c67df7a92d42031d356 |
| architect_review.py | 19a6ea2811a1d43e2068f9b184e4bdc69bc99ad09043f172a6654c7b2bbadac5 |
| test_architect_review.py | c39ccd5fbbd6d66bb53ac2dabfb30be81985ea3afc4ef4102b2ea1ce07330d85 |
| test_supervisor.py | e45080303bccdcf0dc8174e21b8164f5be4d0f64ceb2e4c195ad641a4b61f0f2 |
| roadmap.py | e14ce5c908c9122fcc8c6946730c610402630d75cae4dc80d7b7a4fefd0b7274 |
| test_roadmap.py | 3f109493f20e57f4ad3778d2e9da71f6802c7a0758c893313a3da4ff44c226ba |
| supervisor.json | 588426c47ac2e4214cb2dda03860a1d859a849d749c02de876288a15f4f8b1cf |
| skyrim-dev | b89d8c5a5471c4ab1bb49e7ac6b2568189b8cf579b5ef2e16244c74116d69da4 |

### Live VDS state at capture

- GLOBAL is RUNNING; the service is active and enabled; the healthcheck timer is active and enabled.
- Control plane is VALID at exact SHA 3e7e893b4018b488e158aa5cda977399c0e55a75. M01 is ACTIVE and runtime acceptance remains human-gated.
- Development uses gpt-6-luna/max, capped at 2 workers; live lane worker PIDs: none.
- Architect review uses gpt-6-sol/max, capped at one process. Active reviewer: none; queued reviews: 0; idle reason: remaining roadmap tasks are externally gated.
- Runnable development work: 0. 10 future roadmap tasks remain external-gated. All four current lanes are BLOCKED; the service remains RUNNING with an explicit idle classification.

| Lane | State | HEAD | Exact-SHA CI | Sol decision |
| --- | --- | --- | --- | --- |
| Combat C05 | BLOCKED | 8007bfa4625bd611458cd714241c1ea69dad9abf | Build linux: success for 8007bfa4625bd611458cd714241c1ea69dad9abf; Build windows: success for 8007bfa4625bd611458cd714241c1ea69dad9abf | BLOCK |
| Authority A09 | BLOCKED | c9fae7f73f65d813c3fe3a4284caad71fada53a9 | Build linux: success for c9fae7f73f65d813c3fe3a4284caad71fada53a9; Build windows: success for c9fae7f73f65d813c3fe3a4284caad71fada53a9 | BLOCK |
| Population L05 | BLOCKED | 74d992001cfc63a7bb16ee7418f11bf05de07148 | Build linux: success for 74d992001cfc63a7bb16ee7418f11bf05de07148; Build windows: success for 74d992001cfc63a7bb16ee7418f11bf05de07148 | BLOCK |
| UI U04 | BLOCKED | 6d32bdab944a904098c0c91953fe6d3c0b641753 | Build linux: success for 6d32bdab944a904098c0c91953fe6d3c0b641753; Build windows: success for 6d32bdab944a904098c0c91953fe6d3c0b641753 | BLOCK |

The autonomous reviewer repaired the earlier A06 compile failures without bypassing CI.
It approved A08 only after its exact-SHA gates passed; the current A09 checkpoint is
BLOCKED because ordinary object activation and lock synchronization still lack a settled
authority contract. Combat C05 remains BLOCKED despite exact-SHA CI passing because the
review bundle did not provide sufficient committed-code evidence for its safety checkpoint.
Population progressed from L03 to L05 through bounded same-phase retries and passing exact-SHA
CI; L05 is BLOCKED because the worker reported completion without reviewable changes.
UI U04 remains BLOCKED because source and session-transition evidence is insufficient.
Every phase advance remained gated by the exact-SHA Sol decision. No runtime acceptance or lane approval bypass occurred.

Two temporary memory-safety pauses occurred during validation. The guard preserved dirty
worktrees, memory recovered, healthchecks passed, and the service was resumed without lowering
thresholds. The resumed scheduler cleared the stale pause idle reason. At this capture it is
RUNNING, has no live workers/reviewer, and explicitly classifies future work as externally gated.

No development branch was merged, no destructive Git operation or force push occurred, no CI
gate was bypassed, and M01 runtime acceptance was not recorded. The Windows PC was left running.

## Autonomous review deployment — 2026-09-23T01:39:45Z

The 24/7 supervisor and isolated Sol reviewer are deployed on the VDS. The
service and healthcheck timer are active and enabled; GLOBAL is RUNNING. The
control plane remains VALID at the exact approved SHA.

| Check | Result |
| --- | --- |
| Python compile check | PASS (`supervisor.py`, `architect_review.py`, reviewer tests) |
| Full deterministic supervisor/roadmap/reviewer suite | 148 passed |
| `skyrim-dev self-test` | `SELF_TEST_OK` |
| `skyrim-dev healthcheck` | All five checks passed |
| `skyrim-dev worker-smoke-test` | `WORKER_SMOKE_OK`; worker inbox blocked; no Git/worktree change or persistent worker |
| `skyrim-dev architect-review-smoke-test` | Isolation passed; real gpt-6-sol/max returned strict JSON `RETRY`; no tool events |
| `git diff --check` on infra branch | PASS |

### Runtime snapshot

- Development workers: `gpt-6-luna`, maximum reasoning, cap 2. One live worker at capture: Combat C05 PID `275905`.
- Architect reviewer: `gpt-6-sol`, maximum reasoning, cap 1. One live isolated reviewer at capture: Population L03 PID `277103`; no queued items at that instant.
- Combat C05: `CODING`; the exact-SHA CI result shown is for its previous committed SHA. The staged C05 changes still need a new commit and Linux/Windows CI before any checkpoint approval.
- Authority A06: `NEEDS_SOL_REVIEW`; Build linux failed (run `35792965327`), Build windows was still in progress (run `35792965343`). No approval was applied.
- Population L03: `NEEDS_SOL_REVIEW`; CI not run; worker validation gap records the unavailable xmake/ActorPopulationTests target.
- UI U04: `BLOCKED` by the Sol checkpoint review because supplied source/session-transition evidence was insufficient; its Linux and Windows CI both passed at the reviewed SHA. No phase advance occurred.
- Autonomous decisions applied: C05 bounded `RETRY` after exact-state validation; U04 `BLOCK`. A06 and L03 decisions remained in the review/recovery flow at capture.
- M01 remains `ACTIVE`; runtime acceptance is human-owned. W01–W10 remain externally gated.

Review identity version 2 removes scheduler-only `evaluated_at` fields from both
the state digest and persisted bundle. Queue coalescing retains only current
queued evidence per lane. A compatibility path rechecks a prior actionable
`RETRY` against the exact saved bundle, lane state, control plane, actions, and
retry budget before applying it. High findings can lead to bounded same-phase
repair; critical/high `APPROVE`, low-confidence decisions, and actionless
`RETRY` remain fail-closed.

The installed runtime hashes match the canonical infra source:

- `supervisor.py`: `d6fe564e339d5b8832b9ddb26867f1511c3212d85dfb3c67df7a92d42031d356`
- `architect_review.py`: `43ceb45bea2f9800516f9961637753fefc163b15f0f4c91d8b414ef1474c5623`
- `test_architect_review.py`: `c39ccd5fbbd6d66bb53ac2dabfb30be81985ea3afc4ef4102b2ea1ce07330d85`
- `supervisor.json`: `588426c47ac2e4214cb2dda03860a1d859a849d749c02de876288a15f4f8b1cf`
- `skyrim-dev`: `b89d8c5a5471c4ab1bb49e7ac6b2568189b8cf579b5ef2e16244c74116d69da4`

No development reset, clean, destructive checkout/restore, force push, merge,
CI bypass, or milestone/runtime acceptance occurred. The Windows PC was left
running as requested.

## Historical V3 baseline (2026-09-22)

Captured on 2026-09-22 after installation.

| Check | Result |
| --- | --- |
| `python3 -B -m unittest discover` on VDS | 92 passed |
| `skyrim-dev worker-smoke-test` | `WORKER_SMOKE_OK`; inbox probe `BLOCKED` |
| `skyrim-dev self-test` | `SELF_TEST_OK` |
| `skyrim-dev healthcheck` | all five checks passed |
| `skyrim-dev status` | `PAUSED`; all four lanes still pending review |
| `skyrim-dev roadmap-status` | valid; W01-W10 external-gated |
| `skyrim-dev milestone-status` | M01 `ACTIVE`; M02-M05 planned |
| `skyrim-dev review-status` | C04/L03/U02 current review; A04 checkpoint pending |
| V3.1 durability regression | rollback on failed save; no receipt/archive; same-daemon and restart replay safe |

The service is active in paused mode. The healthcheck timer is active and
enabled. No development worker PID or `codex exec` lane process exists.

The only live mutation fixture was an invalid request with a known safe ID and
an unknown lane. Its receipt was `FAILED`, return code `2`, with reason
`lane command requires one known lane`; it did not mutate lane state.

The V3.1 deterministic save-failure fixtures mutate only isolated test state.
They prove that a failed durable save restores the daemon state and roadmap
snapshot, leaves the request in the inbox without a receipt, and prevents the
next tick from treating the in-memory request marker as durable. The
`sync-control-plane` fixture models its real fetch/fast-forward/cache behavior
as an idempotent external action and proves safe replay after a pre-commit
failure.
