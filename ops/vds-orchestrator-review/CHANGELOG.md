## V3.3 decision-drain and worker-smoke follow-up — 2026-09-23

- Fixed autonomous review starvation by applying and revalidating completed Sol decisions before admitting another queued review; a still-pending result blocks admission while the existing exact-state stale checks and approval gates remain in force.
- Corrected the worker-smoke fixture to match the prompt's byte-exact input and added a regression test.
- Canonical and installed deterministic suites pass 190 tests each; compile checks, self-test, healthcheck, real worker smoke, and isolated architect-review smoke pass.
- Deployed the follow-up with pre-deployment copies under `/var/lib/skyrim-dev/backups/review-decision-drain-20260923T121422Z`. Live evidence is recorded in the dated follow-up report and redacted state snapshot.

## V3.3 exact-SHA Sol evidence and blocked-lane scheduler repair — 2026-09-23

- Replaced the shadowed worktree-only review bundle path with one canonical
  evidence-v3 builder based on the last trusted accepted phase HEAD through the
  exact reviewed commit. It includes raw diff SHA-256, rename-aware file
  inventory, all phase commits/messages, and exact reviewed-Git-object content
  hashes/source context; dirty diffs remain a separate input.
- Added exact-SHA CI, required product/roadmap context, plan/product/context
  hashes, fail-closed bounds, immutable v3 identities/bundles, and a reviewer
  prompt that distinguishes committed evidence from uncommitted worktree data.
- Added `REVIEW_EVIDENCE_ERROR` with bounded retries and explicit infrastructure
  escalation, while allowing independent lanes to queue. Reviewer status reports
  evidence errors; evidence failures never become product BLOCK decisions.
- Fixed scheduler state so a terminal exact-head Sol BLOCK is `BLOCKED_REVIEW`,
  not `READY`; idle summaries identify active blocked lanes and future external
  gates.
- Added guarded Population L05 checkpoint reconciliation using the normal
  completion/checkpoint path and exact committed-range, clean-tree, structural,
  CI, and control-plane invariants. It performs no Sol approval or milestone
  acceptance.
- Added an exact-target `re-review-v3` operator request that runs Sol-only while
  globally paused, supersedes matching legacy decision metadata, and keeps
  development paused until the four v3 decisions finish.
- Added 34 exact-evidence/paused-continuation regressions and four scheduler/evidence-error
  regressions. The complete deterministic suite passes 187 tests; deployment
  and live review outcomes are recorded in the dated verification receipt.
- Clarified the Sol output contract: APPROVE must have an empty required_actions list, and any needed repair or validation requires RETRY. The semantic validator remains fail-closed; an existing regression now checks the prompt.

# Supervisor Hardening V2 changelog

## Isolated Sol architect review — 2026-09-23

- Added a serialized `gpt-6-sol/max` reviewer, strict identity-bound JSON decisions, immutable redacted evidence bundles, bounded retries/backoff, and stale-state checks.
- The model receives the evidence inline and has no shell, MCP, browser, plugin, or network tools. Bubblewrap hides operator state, worker trees, service files, logs, and host credentials; any unexpected tool event fails the review.
- Added the real-model isolation smoke command and deterministic reviewer tests. The final Linux suite passed 149 tests and the smoke decision returned `RETRY` for the deliberately unsafe fixture with no tool events.
- Follow-up stabilized review identity by recursively removing scheduler-only `evaluated_at` fields from the state digest and persisted immutable bundle, versioned the bundle identity to avoid legacy collisions, and coalesced superseded queue entries.
- RETRY policy now permits actionable high-severity findings within the existing bounds; APPROVE still fails closed on critical/high findings, and low-confidence or actionless RETRY is blocked. One-time exact-state reevaluation recovers an actionable RETRY blocked by the earlier over-broad severity rule.
- Fixed stale idle-reason reporting after recovery from a safety pause; added a deterministic regression test.
- Configured the reviewer without changing the exact control-plane SHA or the preserved lane worktrees. No milestone/runtime acceptance was recorded.


## Phases 5–10 worker-model and recovery maintenance — 2026-09-23

- Changed only the future development worker model arguments to gpt-6-luna; max reasoning, review configuration, approval/sandbox arguments, and max_concurrent_workers=2 remain unchanged.
- Synchronized the canonical infra snapshot with the installed hard two-worker recovery admission guard and its deterministic regression tests.
- Corrected ownership of all 20 root-owned loose-object fan-out directories to skyrimdev:skyrimdev while preserving their modes and object files; the same fault had also surfaced in Authority A06. Population's isolated temporary-index check passed with the real index unchanged and all six retained file hashes unchanged.
- Passed the 101-test supervisor/roadmap suite, self-test, MODEL_OK probe, and disposable worker smoke. Approved only the specified Combat, Authority, and UI checkpoints; retried Population L03 and did not approve it.
- Resumed only after Phase 8 passed. Thirty-one samples over five minutes observed a maximum of two development workers with the expected model and reasoning. Combat's new C05 review packet remains unapproved.

## V3.2 bounded supervisor concurrency repair — 2026-09-22

- Made `schedule()` the sole development-worker admission authority by keeping
  `RECOVERING` runnable but removing its direct launch from `advance_lane()`.
- Added a shared worker-limit helper and a defense-in-depth cap check in
  `start_worker()`, counting normal and recovery processes identically.
- Added eight deterministic concurrency regressions covering resume with three
  recoveries, scheduler bypass, direct admission, slot refill, review gating,
  CI/non-worker states, rate-limit probes, and spawn failure.
- Updated the observer self-test to recognize a paused current-review lane with
  a preserved dirty recovery worktree without changing any development lane
  decision.
- Installed and verified the production source while globally paused: 101 tests,
  self-test, healthcheck, disposable worker smoke, and all status paths passed.
- No development lane was approved, retried, blocked, advanced, committed,
  pushed, merged, reset, cleaned, checked out, restored, or discarded.

## V3.1 durability repair — 2026-09-22

- Made each daemon-owned operator request transactional across dispatch and
  durable state commit: state, processed-request ledger, and active roadmap
  snapshot roll back on save refusal or exception.
- Failed saves retain the inbox request, emit no receipt, archive nothing, and
  stop the current request pass so a later tick cannot trust an in-memory
  processed marker.
- Added deterministic same-daemon retry, restart replay, duplicate suppression,
  stale-memory, and idempotent `sync-control-plane` replay coverage.
- Updated the final service/timer and lane status evidence; the suite now
  passes 92 tests while global mode remains `PAUSED`.

## V3 bounded infrastructure repair — 2026-09-22

- Replaced observer-process runtime mutations with a daemon-owned, durable
  operator request inbox, typed request validation, atomic receipts, bounded
  processed-request history, replay-safe restart handling, and fail-closed
  daemon-inactive behavior.
- Added the `COMPLETE_WITH_VALIDATION_GAP` worker result and persisted bounded
  gap evidence without weakening structural validation or required exact-SHA
  GitHub CI gates.
- Added temporary-index prospective commit validation for tracked, deleted,
  renamed, staged-plus-unstaged, and untracked files before real index staging;
  retained the real cached diff check before commit.
- Strengthened cross-lane scheduler tests and idle status observability while
  preserving dependency order and external gates.
- Preserved C04/L03/U02 dirty recovery worktrees and A04's pending checkpoint;
  no development lane was retried, approved, advanced, committed, pushed, or
  merged.
- Final VDS verification: 87 unit tests passed, self-test passed, healthcheck
  passed, and the disposable worker smoke test proved the operator inbox was
  blocked under `workspace-write`.

## Final scoped sandbox remediation — 2026-09-22

- Installed only the required Ubuntu packages: `apparmor-profiles`,
  `apparmor-utils`, and the existing `bubblewrap` package; no distribution
  upgrade, Docker installation, kernel setting, or global namespace-policy
  relaxation was performed.
- Installed the official
  `/usr/share/apparmor/extra-profiles/bwrap-userns-restrict` policy exactly at
  `/etc/apparmor.d/bwrap-userns-restrict` and loaded it with
  `apparmor_parser -r`. The enforced kernel profiles are `bwrap` and
  `unpriv_bwrap`.
- Preserved `kernel.apparmor_restrict_unprivileged_userns=1` and
  `kernel.unprivileged_userns_clone=1`. Direct user and network bwrap probes
  returned `rc=0` with no error output.
- The production disposable worker smoke test now passes all isolation checks
  and returns `WORKER_SMOKE_OK` while the orchestrator, timer, global pause,
  and no-worker state remain unchanged.
- Re-ran the complete 65-test suite and all observer paths; the persisted state
  hash remained unchanged before and after the observer commands.

## Runtime-owner / observer separation + worker sandbox repair — 2026-09-22

- Separated `Supervisor(runtime_owner=True)` daemon construction from the
  default observer/operator construction. `status`, `roadmap-status`,
  `milestone-status`, `review-status`, `healthcheck`, and `self-test` no longer
  reconcile persisted worker ownership or stale Codex probes merely because
  their local process table is empty.
- Moved daemon-lock acquisition before runtime-owner construction and startup
  reconciliation. A second daemon exits without rewriting live-worker state;
  legitimate restart recovery remains available only to the lock owner.
- Kept explicit operator mutations narrow and opt-in, and added regression
  coverage for live CODING ownership, RATE_LIMITED probes, operator commands,
  lock ordering, restart recovery, observer commands, credential isolation,
  local-source prompt policy, and disposable smoke-workspace use.
- Added `skyrim-dev worker-smoke-test`. It runs the official Codex CLI as
  `skyrimdev` with `gpt-5.6-luna`, `max`, `approval_policy=never`, and
  `workspace-write` in a disposable directory, then removes that directory.
- Before the final scoped remediation, the smoke test was intentionally
  fail-closed on this VDS: Ubuntu AppArmor's
  `unprivileged_userns` profile prevents bubblewrap's required user/network
  namespace setup. No unrestricted fallback or production policy relaxation
  was installed; autonomous development remains disabled.
- Preserved the exact C03/A04 review states and documented the complete first-
  run logs: C03 was operator-paused before emitting a real result marker, while
  A04 established the `DrawWeaponRequest`/`OwnershipEpoch` stale-reacquisition
  finding before its workspace-write failure.

## Roadmap / Dependency Control Plane — 2026-09-21

- Created the dedicated `/srv/projects/skyrim-online-str/control-plane`
  worktree on `orchestration/control-plane` at
  `3e7e893b4018b488e158aa5cda977399c0e55a75` and made synchronization
  fetch/clean-branch/fast-forward-only and fail closed.
- Added strict roadmap parsing for milestones, workstreams, existing PLAN
  queues, roadmap tasks, dependencies, cycles, external gates, future lanes,
  executable flags, risk, Sol-review requirements, and acceptance criteria.
- Added persisted task identity, dependency decisions, exact SHA/definition
  approvals, bounded audit history, round-robin fairness, reboot-safe cursor,
  global rate-limit gating, and the M01 runtime-acceptance state machine.
- Preserved the existing C03/A04/L03/U02 positions and left M01-WORLD W01-W10
  blocked on its canonical reviewed integration-branch gate. No future branch
  was created and no development phase was started.
- Added 20 deterministic roadmap/control-plane tests; the combined suite now
  passes 51 tests.
- Installed the repository-required Node 20/pnpm 9 UI tooling with bounded
  service-owned caches. No UI dependencies, Docker, or heavyweight build
  infrastructure were installed.

## V2.1 — 2026-09-21

- Removed the duplicate `git_dirty()` definition and made Git-status failure
  dirty/unsafe everywhere; `git_status_files()` now reports unreadable status
  explicitly so commit and push gates cannot fail open.
- Reconciled stale persisted Codex rate-limit probes on supervisor restart,
  preserving the existing bounded backoff and retry count without spending lane
  recovery budget or causing an immediate retry storm.
- Made every `CURRENT_PHASE_REVIEW` retry enter `RECOVERING`, including clean
  worktrees and paused/resume flows, while preserving same-phase correction
  semantics.
- Added bounded, redacted recovery context containing the prior failure, CI
  failure excerpt, and relevant worker evidence; normal prompts remain free of
  stale recovery data and workers are not asked to query GitHub.
- Included reasonable-size textual untracked files as bounded new-file diffs in
  risk analysis and review packets. Binary, unreadable, symlink-escaping, and
  oversize files are metadata-only and require safe review.
- Added 12 deterministic V2.1 regression tests, bringing the supervisor suite
  to 31 passing tests.

## 2026-09-21

- Fixed multi-workflow CI aggregation: exact commit-SHA filtering, per-workflow grouping, newest-attempt selection, all-required-workflow waiting, bounded queue/completion timeouts, persisted per-workflow evidence, and failure propagation.
- Fixed the checkpoint off-by-one so the third successful phase since the last approved review triggers review immediately.
- Added explicit `POST_PHASE_CHECKPOINT`, `CURRENT_PHASE_REVIEW`, and `FINAL_MILESTONE_OR_QUEUE_REVIEW` metadata and safe `review-status`, `approve`, `retry`, and `block` commands.
- Made post-phase approval reset the success counter and advance exactly once; current-phase approval refuses and requires retry.
- Replaced incomplete dirty detection with NUL-delimited porcelain Git status covering staged/unstaged changes, deletions, renames, untracked files, and conflicts.
- Added exact-branch checks before workers, clean-worktree requirements for normal workers, bounded dirty recovery, combined staged/unstaged diff review, and post-commit cleanliness verification.
- Replaced permanent lane logs with unique per-attempt logs, persisted active log paths, current-log-only marker parsing, and bounded retention.
- Made operator and automatic safety pauses terminate active workers, preserve diffs, and freeze all commit/push/phase mutations; safety resume waits for conditions to clear.
- Added persistent global Codex availability state, conservative usage/rate-limit classification, bounded 15/30/60-minute sleep/resume backoff, one-probe retry behavior, and status output.
- Added no-change phase safety: a `COMPLETE` worker with no reviewable changes goes to `NEEDS_SOL_REVIEW`.
- Added configurable changed-file and total-diff-byte bounds and root-level generated-artifact rejection; explicit validated paths are staged instead of `git add -A`.
- Separated structural validation, configured focused-test evidence, and GitHub CI evidence; unconfigured focused tests are explicitly reported as not independently verified.
- Installed canonical `PRODUCT_VISION.md`, `WORLD_RULES.md`, and `MILESTONE_01_CORE_WORLD.md` product context and added bounded lane-relevant prompt injection.
- Added the `RoadmapTask` adapter so a future milestone/dependency scheduler can supply milestone, task ID, dependencies, risk, review policy, and acceptance criteria without changing worker/CI machinery.
- Hardened worker environments by removing GitHub/SSH credential variables and redirecting `GH_CONFIG_DIR` to an empty service-owned location; documented the same-account residual risk.
- Added 19 deterministic supervisor tests covering CI aggregation, checkpoint/review state, Git status variants, stale logs, no-change/diff bounds, pause, resource safety, and Codex availability/backoff.
- Updated the installed VDS files, redacted review snapshot, state projection, verification evidence, architecture notes, residual risks, and secret scan while keeping autonomous development disabled.

No development lane branch was modified, pushed, or merged by this V2 pass.
