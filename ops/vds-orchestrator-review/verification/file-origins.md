# V2.1 file origins

- `source/orchestrator/supervisor.py`: V1 installed supervisor reviewed at
  architect snapshot commit `7aa07f042fe21da3e0ac87cca6870d95f0f11ebe`, then
  hardened in V2 and corrected in this V2.1 pass.
- `source/orchestrator/roadmap.py`: new V2 standard-library adapter for the
  future milestone/dependency scheduler.
- `source/orchestrator/test_supervisor.py`: V2 tests plus 12 deterministic V2.1
  regression tests; 31 tests pass in the installed suite.
- `source/config/supervisor.json`: V1 installed configuration extended with V2
  workflow, evidence, bounds, product, worker-isolation, and retry settings.
- `source/management/skyrim-dev`: V1 management wrapper extended with explicit
  review lifecycle commands.
- `source/product/*.md`: new canonical product-context files requested for V2.
- `source/systemd/*`: existing paused/disabled unit definitions republished;
  no unit was enabled or started for autonomous development.
- `state/persistent-lane-state.json`: redacted projection of the installed V2
  state, preserving the four protected development heads.
