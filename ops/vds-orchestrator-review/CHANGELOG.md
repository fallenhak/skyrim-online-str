# Supervisor Hardening V2 changelog

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
