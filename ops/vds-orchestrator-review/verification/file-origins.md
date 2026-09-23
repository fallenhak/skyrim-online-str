# Roadmap / dependency control-plane file origins

- `source/orchestrator/supervisor.py`: V1 installed supervisor reviewed at
  architect snapshot commit `7aa07f042fe21da3e0ac87cca6870d95f0f11ebe`, then
  hardened in V2 and corrected in this V2.1 pass.
- `source/orchestrator/roadmap.py`: standard-library control-plane parser,
  normalized task model, dependency evaluator, exact approval binding, and
  round-robin helper.
- `source/orchestrator/test_supervisor.py`: 45 V2/V2.1 plus runtime-owner,
  observer, lock, prompt, credential, and smoke regressions.
- `source/orchestrator/test_roadmap.py`: 20 deterministic roadmap/control-plane
  tests; 65 tests pass in the installed suite.
- `source/config/supervisor.json`: V1 installed configuration extended with V2
  workflow, evidence, bounds, product, worker-isolation, and retry settings.
- `source/management/skyrim-dev`: management wrapper extended with control-plane,
  roadmap, task-approval, milestone-acceptance, and disposable worker-smoke
  commands.
- `source/product/*.md`: canonical copies from control-plane SHA
  `3e7e893b4018b488e158aa5cda977399c0e55a75`.
- `source/systemd/*`: existing paused/disabled unit definitions republished;
  no unit was enabled or started for autonomous development.
- `verification/control-plane-schema.md`: schema/parser/scheduler contract.
- `verification/first-run-blockers.md`: complete C03/A04 log findings and
  preserved A04 stale-incarnation evidence.
- `verification/sandbox-diagnosis.md`: VDS bubblewrap/AppArmor diagnosis and
  explicit no-fallback boundary.
- `verification/worker-smoke-results.md`: disposable worker smoke contract and
  fail-closed result.
- `state/persistent-lane-state.json`: redacted projection of the installed V3
  scheduler state, preserving the four protected development heads.

## V3 bounded repair additions

- `source/orchestrator/supervisor.py`: daemon-owned operator request lifecycle,
  exactly-once processed ledger, validation-gap protocol, temporary-index
  prospective diff check, review-gated scheduler summary, and preserved review
  worktree self-test behavior.
- `source/orchestrator/test_supervisor.py`: 92-test deterministic suite,
  including operator ownership/replay, malformed/inactive requests,
  validation gaps, prospective Git checks, and cross-lane scheduling.
- `source/config/supervisor.json`: private operator request root and bounded
  request/receipt settings.
- `source/management/skyrim-dev`: wrapper documentation stating that mutation
  commands are submitted to the live daemon.
- `verification/*-v3.md` and `verification/operator-request-design.md`:
  secret-free design and final evidence captured from the VDS.
- `verification/operator-request-durability.md`: V3.1 transactional rollback,
  retained-inbox, restart-replay, duplicate, and idempotent sync evidence.

## V3.2 bounded concurrency repair additions

- `source/orchestrator/supervisor.py`: single scheduler admission for
  `READY`/`RECOVERING` workers, defense-in-depth worker cap, and paused-review
  self-test recognition.
- `source/orchestrator/test_supervisor.py`: 101-test deterministic suite with
  the production resume path, direct admission, refill, review, non-worker,
  rate-limit, failure, and paused-review regressions.
- `verification/architect-review-20260922/v32-concurrency-repair.md`:
  secret-free production guard, exact protected-lane snapshot, bounded worker
  output evidence, validation results, and final state.
