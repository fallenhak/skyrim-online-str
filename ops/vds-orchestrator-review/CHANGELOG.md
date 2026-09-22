# Supervisor Hardening V2 changelog

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
  passes 91 tests while global mode remains `PAUSED`.

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
