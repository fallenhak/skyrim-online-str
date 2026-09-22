# Cross-lane scheduling and idle status

Review-gated lanes remain parked and consume no worker slot. The scheduler
continues round-robin selection among independent `READY` lanes up to the
configured concurrency limit of two. A lane waiting for CI/checkpoint review
does not block unrelated ready lanes. If every current lane is gated, no
worker starts; future `BLOCKED_EXTERNAL_GATE` tasks remain blocked and no
dependency is skipped.

Deterministic tests cover:

- combat in `NEEDS_SOL_REVIEW` with authority/population/UI ready: two
  non-combat lanes are selected;
- combat review plus authority waiting for CI with population/UI ready:
  population and UI remain schedulable;
- all four current lanes review-gated: no worker starts and the daemon remains
  healthy;
- all current lanes gated with W01-W10 externally blocked: no speculative
  future task starts.

When global mode is `RUNNING` and no current task is schedulable, `status`
prints `RUNNABLE WORK: 0` plus review-gated lanes, waiting checkpoint lanes,
and the count of externally gated future tasks. Status never persists its
observer state.
