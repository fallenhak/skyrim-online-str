# Installed source map

The copies under `source/` are byte-for-byte copies of the active VDS files named in the right-hand column.

| Review file | Installed source | Responsibility |
| --- | --- | --- |
| `source/orchestrator/supervisor.py` | `/srv/services/skyrim-dev/orchestrator/supervisor.py` | Single local Python module: state machine, worker invocation, watchdog/recovery, resource/Git guards, validation, outer Git commit/push, GitHub Actions polling, review checkpoints, healthcheck, self-test |
| `source/config/supervisor.json` | `/srv/services/skyrim-dev/config/supervisor.json` | Repository, lane/worktree, limits, timeouts, forbidden prefixes, issue mappings |
| `source/management/skyrim-dev` | `/usr/local/bin/skyrim-dev` | Operator command wrapper and root/service-user policy |
| `source/systemd/skyrim-dev-orchestrator.service` | `/etc/systemd/system/skyrim-dev-orchestrator.service` | Long-running supervisor unit |
| `source/systemd/skyrim-dev-healthcheck.service` | `/etc/systemd/system/skyrim-dev-healthcheck.service` | Oneshot healthcheck unit |
| `source/systemd/skyrim-dev-healthcheck.timer` | `/etc/systemd/system/skyrim-dev-healthcheck.timer` | Ten-minute healthcheck schedule; currently disabled |
| `source/logrotate/skyrim-dev` | `/etc/logrotate.d/skyrim-dev` | Daily, 14-copy, 10 MiB log rotation |

## `supervisor.py` navigation

- Secret redaction and atomic JSON persistence: lines 46-90.
- Plan parsing and persisted phase selection: lines 210-258.
- Child environment sanitization and command execution: lines 260-313.
- Disk/memory and Git health guards: lines 316-402.
- Codex worker prompt, invocation, bounded logs, timeout/stale detection: lines 404-599.
- Diff policy, local validation, outer commit and push: lines 600-758.
- GitHub Actions polling and bounded failure handling: lines 759-829.
- Sol review triggers, review packet generation, issue update, and phase advancement: lines 830-1028.
- Deterministic scheduling, main loop, shutdown, and status output: lines 1029-1095.
- Healthcheck and orchestrator self-test: lines 1096-1164.
- Control commands, lock acquisition, and restart-safe run entrypoint: lines 1165-1222.

No auth file, credential store, SSH key, environment secret, cookie, raw runtime log, command history, or Codex conversation log is part of this snapshot.
