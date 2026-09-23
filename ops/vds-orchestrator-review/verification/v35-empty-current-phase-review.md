# V3.5 empty current-phase review recovery — 2026-09-23

## Root cause

The supervisor could retain `NEEDS_SOL_REVIEW/CURRENT_PHASE_REVIEW` after a
phase transition or interrupted worker even when no current-phase result or
diff existed. The trusted prior accepted HEAD was also used as the lane's last
commit, but no phase-bound worker identity distinguished that old HEAD from
current work. Evidence construction then failed, and the terminal evidence
error kept the scheduler lane review-gated.

## Repair

- Persist `worker_phase_id`, worker start HEAD, exit code, and interruption
  status so worker output is tied to the phase that launched it.
- Classify current-phase review evidence from structured worker state, the
  trusted prior accepted phase HEAD, exact Git ancestry/diff, and bounded
  worktree safety checks. The prior phase HEAD by itself is empty evidence.
- Retire empty stale gates and matching evidence errors/queued review items as
  stale infrastructure state. Preserve review packets, lane history, and dirty
  worktree bytes. Clean untouched phases return to `READY`; safe dirty
  interrupted work returns to one bounded `RECOVERING` attempt. Ambiguous state
  remains fail-closed.
- Reset phase-local worker, validation, CI, review-reason, and retry metadata
  after checkpoint approval or phase advancement. Completed checkpoint review
  metadata remains available as history.
- Keep ordinary review/development at GPT-6 Luna/max, architect escalation at
  GPT-6 Sol/medium, worker/reviewer caps at 2/1, exact-SHA CI, and the control
  plane unchanged.

## Deterministic validation

- `py -3 -m py_compile supervisor.py architect_review.py test_supervisor.py` —
  passed.
- `py -3 -B -m unittest discover -v` — 215 tests passed, including seven new
  V3.5 cases for stale gate retirement, queue suppression, checkpoint
  advancement, interrupted dirty/clean work, committed current-phase diffs,
  and explicit current-phase worker review results.
- Existing tests continue to cover duplicate admission protection, the two
  worker and one reviewer caps, Luna/max routing, exact-SHA CI, and control
  plane validation.

Live deployment and lane reconciliation are recorded in the task completion
receipt after the bounded runtime checks.
