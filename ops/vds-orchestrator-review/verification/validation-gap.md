# Worker result validation-gap protocol

The worker protocol now distinguishes four outcomes:

- `WORKER_RESULT: COMPLETE`: implementation is complete and required/available
  checks passed.
- `WORKER_RESULT: COMPLETE_WITH_VALIDATION_GAP`: implementation is complete,
  available checks passed, a local validation tool was unavailable, and no
  known check failed. The worker must immediately emit one bounded
  `VALIDATION_GAP:` reason.
- `WORKER_RESULT: NEEDS_SOL_REVIEW`: a genuine design, authority, security,
  product, or source-evidence decision needs architect review.
- `WORKER_RESULT: BLOCKED`: a real blocker prevents safe completion.

Missing xmake/Catch2 alone is therefore a validation gap, not a blocker. A
command that actually fails is never converted into a gap.

Both complete markers enter `LOCAL_VALIDATION`. The supervisor preserves the
marker and bounded reason in lane state, validation evidence, phase history,
review packets, and status output. It still runs all structural checks, the
prospective Git check, configured focused checks when present, the trusted
commit path, push, and exact-SHA Build linux/Build windows CI. A focused or
structural failure changes the gap to `INVALIDATED` and follows the existing
bounded recovery/review path.

The worker prompt was updated with the exact semantics and a required
`VALIDATION_GAP:` line. The installed focused-validation map remains empty, so
the supervisor records that no focused command was independently configured;
required GitHub CI remains mandatory.
