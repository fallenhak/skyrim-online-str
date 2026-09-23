# Source-to-installed-path map

| Review snapshot | Installed path | Purpose |
| --- | --- | --- |
| `source/orchestrator/architect_review.py` | `/srv/services/skyrim-dev/orchestrator/architect_review.py` | Two-tier tool-free reviewer (Luna/max normal, Sol/medium deterministic architect escalation), strict decisions, v3 exact-commit evidence, bounded availability/evidence retry, and blocked/idle state |
| `source/orchestrator/test_architect_review.py` | `/srv/services/skyrim-dev/orchestrator/test_architect_review.py` | Deterministic reviewer schema, exact-SHA evidence, isolation, tool-free invocation, decision/action consistency, and tier/retry policy tests |
| `source/orchestrator/supervisor.py` | `/srv/services/skyrim-dev/orchestrator/supervisor.py` | Runtime-owner/observer-safe supervisor, fail-closed gates, quota-aware model routing, and worker smoke test |
| `source/orchestrator/roadmap.py` | `/srv/services/skyrim-dev/orchestrator/roadmap.py` | Validated roadmap schema, dependency scheduler, task identity, gates, and fairness |
| `source/orchestrator/test_supervisor.py` | `/srv/services/skyrim-dev/orchestrator/test_supervisor.py` | Supervisor, runtime-owner, observer, scheduler, evidence-error, quota routing, exact-SHA, and smoke regressions |
| `source/orchestrator/test_roadmap.py` | `/srv/services/skyrim-dev/orchestrator/test_roadmap.py` | 20 deterministic control-plane and scheduler tests |
| `source/config/supervisor.json` | `/srv/services/skyrim-dev/config/supervisor.json` | Required workflows, bounds, paths, retry policy |
| `source/management/skyrim-dev` | `/usr/local/bin/skyrim-dev` | Operator command wrapper, including worker and Luna/architect reviewer smoke tests |
| `source/product/PRODUCT_VISION.md` | `/srv/services/skyrim-dev/product/PRODUCT_VISION.md` | Product vision context |
| `source/product/WORLD_RULES.md` | `/srv/services/skyrim-dev/product/WORLD_RULES.md` | Immutable world rules |
| `source/product/MILESTONE_01_CORE_WORLD.md` | `/srv/services/skyrim-dev/product/MILESTONE_01_CORE_WORLD.md` | First playable target |
| `verification/control-plane-schema.md` | review snapshot only | Roadmap schema, task identity, control mutation, and acceptance contract |
| `verification/first-run-blockers.md` | review snapshot only | Complete C03/A04 log findings and classifications |
| `verification/sandbox-diagnosis.md` | review snapshot only | VDS bwrap/AppArmor root-cause evidence and remediation boundary |
| `verification/worker-smoke-results.md` | review snapshot only | Disposable Codex worker sandbox smoke-test contract and result |
| `source/systemd/*.service` | `/etc/systemd/system/*.service` | Paused-by-default runtime units |
| `source/systemd/*.timer` | `/etc/systemd/system/*.timer` | Disabled healthcheck schedule |

The snapshot also contains a redacted state projection, verification evidence,
architecture notes, changelog, residual risks, and checksums. It intentionally
does not contain credentials or runtime log content.
