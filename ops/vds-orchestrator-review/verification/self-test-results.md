# V2.1 verification results

Captured on the VDS on 2026-09-21 after installation. All commands were run
without starting the orchestrator or healthcheck timer.

## Supervisor unit tests

Command:

```text
cd /srv/services/skyrim-dev/orchestrator
runuser -u skyrimdev -- env PYTHONPATH=/srv/services/skyrim-dev/orchestrator PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v test_supervisor
```

Result: 31 tests passed, including the 12 V2.1 regression tests for
fail-closed Git status, restart-safe rate-limit probes, current-phase retry
semantics, bounded recovery context, and untracked-file review bounds.

## `skyrim-dev self-test`

```text
PASS Codex CLI installed
PASS Codex ChatGPT authentication
PASS GitHub CLI authentication
PASS GitHub push permission
PASS primary repository and four worktrees
PASS disk and memory guard
PASS worker GitHub credential isolation
PASS required CI workflow configuration
PASS empty worker GitHub config directory
PASS Codex usage-limit classifier safety
PASS product context files
PASS combat worktree clean
PASS combat safe push dry-run
PASS authority worktree clean
PASS authority safe push dry-run
PASS population worktree clean
PASS population safe push dry-run
PASS ui worktree clean
PASS ui safe push dry-run
PASS GitHub Actions polling
PASS state persistence write
SELF_TEST_OK
```

## `skyrim-dev healthcheck`

```text
PASS resource guard
PASS git/worktree health
PASS state persistence: /var/lib/skyrim-dev/state/state.json
PASS product context
```

## Safety state at capture

```text
orchestrator: inactive, disabled
healthcheck timer: inactive, disabled
global mode: PAUSED
development workers launched: no
development branch heads changed: no
```

Protected development heads verified unchanged:

```text
combat:     905cf71c55200509702fb299aaa953ae46dcb374
authority:  caf7dcc31ca4b6d0912f31b151408ba24c13938c
population: 52c97ba4d5da993e6ef2fa4bdf398f13c22b456a
ui:         a473531ad16a82cecc8a4cdecc460934ba7efcfa
```

The only checkout changed by this pass was the review branch
`infra/vds-orchestrator-review`; no development lane branch was edited,
pushed, or merged.
