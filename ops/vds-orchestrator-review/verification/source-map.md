# Source-to-installed-path map

| Review snapshot | Installed path | Purpose |
| --- | --- | --- |
| `source/orchestrator/architect_review.py` | `/srv/services/skyrim-dev/orchestrator/architect_review.py` | Isolated tool-free Sol review, strict decision validation, stable versioned evidence identity, bounded lifecycle, and accurate resumed idle state |
| `source/orchestrator/test_architect_review.py` | `/srv/services/skyrim-dev/orchestrator/test_architect_review.py` | 47 deterministic reviewer schema, isolation, tool-free invocation, and policy tests |
| `source/orchestrator/supervisor.py` | `/srv/services/skyrim-dev/orchestrator/supervisor.py` | Runtime-owner/observer-safe supervisor, fail-closed gates, and worker smoke test |
| `source/orchestrator/roadmap.py` | `/srv/services/skyrim-dev/orchestrator/roadmap.py` | Validated roadmap schema, dependency scheduler, task identity, gates, and fairness |
| `source/orchestrator/test_supervisor.py` | `/srv/services/skyrim-dev/orchestrator/test_supervisor.py` | 82 supervisor, runtime-owner, observer, lock, prompt, and smoke regressions |
| `source/orchestrator/test_roadmap.py` | `/srv/services/skyrim-dev/orchestrator/test_roadmap.py` | 20 deterministic control-plane and scheduler tests |
| `source/config/supervisor.json` | `/srv/services/skyrim-dev/config/supervisor.json` | Required workflows, bounds, paths, retry policy |
| `source/management/skyrim-dev` | `/usr/local/bin/skyrim-dev` | Operator command wrapper, including `worker-smoke-test` and `architect-review-smoke-test` |
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
