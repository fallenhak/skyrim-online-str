# Source-to-installed-path map

| Review snapshot | Installed path | Purpose |
| --- | --- | --- |
| `source/orchestrator/supervisor.py` | `/srv/services/skyrim-dev/orchestrator/supervisor.py` | V2.1 supervisor state machine and fail-closed gates |
| `source/orchestrator/roadmap.py` | `/srv/services/skyrim-dev/orchestrator/roadmap.py` | Validated roadmap schema, dependency scheduler, task identity, gates, and fairness |
| `source/orchestrator/test_supervisor.py` | `/srv/services/skyrim-dev/orchestrator/test_supervisor.py` | 31 V2.1 supervisor tests and temporary-Git fixtures |
| `source/orchestrator/test_roadmap.py` | `/srv/services/skyrim-dev/orchestrator/test_roadmap.py` | 20 deterministic control-plane and scheduler tests |
| `source/config/supervisor.json` | `/srv/services/skyrim-dev/config/supervisor.json` | Required workflows, bounds, paths, retry policy |
| `source/management/skyrim-dev` | `/usr/local/bin/skyrim-dev` | Operator command wrapper |
| `source/product/PRODUCT_VISION.md` | `/srv/services/skyrim-dev/product/PRODUCT_VISION.md` | Product vision context |
| `source/product/WORLD_RULES.md` | `/srv/services/skyrim-dev/product/WORLD_RULES.md` | Immutable world rules |
| `source/product/MILESTONE_01_CORE_WORLD.md` | `/srv/services/skyrim-dev/product/MILESTONE_01_CORE_WORLD.md` | First playable target |
| `verification/control-plane-schema.md` | review snapshot only | Roadmap schema, task identity, control mutation, and acceptance contract |
| `source/systemd/*.service` | `/etc/systemd/system/*.service` | Paused-by-default runtime units |
| `source/systemd/*.timer` | `/etc/systemd/system/*.timer` | Disabled healthcheck schedule |

The snapshot also contains a redacted state projection, verification evidence,
architecture notes, changelog, residual risks, and checksums. It intentionally
does not contain credentials or runtime log content.
