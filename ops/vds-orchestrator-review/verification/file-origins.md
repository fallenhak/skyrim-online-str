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
