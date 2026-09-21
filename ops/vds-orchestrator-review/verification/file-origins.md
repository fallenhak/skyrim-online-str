# Review file origins

- `source/orchestrator/supervisor.py` <- `/srv/services/skyrim-dev/orchestrator/supervisor.py`
- `source/config/supervisor.json` <- `/srv/services/skyrim-dev/config/supervisor.json`
- `source/management/skyrim-dev` <- `/usr/local/bin/skyrim-dev`
- `source/systemd/skyrim-dev-orchestrator.service` <- `/etc/systemd/system/skyrim-dev-orchestrator.service`
- `source/systemd/skyrim-dev-healthcheck.service` <- `/etc/systemd/system/skyrim-dev-healthcheck.service`
- `source/systemd/skyrim-dev-healthcheck.timer` <- `/etc/systemd/system/skyrim-dev-healthcheck.timer`
- `source/logrotate/skyrim-dev` <- `/etc/logrotate.d/skyrim-dev`
- `state/persistent-lane-state.json` <- redacted projection of `/var/lib/skyrim-dev/state/state.json`
- `README.md`, `verification/*` <- review documentation generated from the installed files and bounded checks
