# Roadmap / dependency control-plane verification results

Captured on the VDS on 2026-09-21 after installation. All commands were run
without starting the orchestrator or healthcheck timer.

## Unit tests

Command:

    cd /srv/services/skyrim-dev/orchestrator
    runuser -u skyrimdev -- env HOME=/home/skyrimdev PYTHONPATH=/srv/services/skyrim-dev/orchestrator PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s . -p 'test_*.py' -v

Result: 51 tests passed: the 31 V2.1 supervisor tests plus 20 deterministic
roadmap/control-plane tests.

## skyrim-dev self-test

Result: SELF_TEST_OK.

The self-test passed Codex/GitHub authentication, push permission, primary
repository and four worktrees, control-plane branch/worktree/roadmap
validation, resource guard, worker GitHub credential isolation, required
workflow configuration, product context, four clean development worktrees,
safe push dry-runs, GitHub Actions polling, and state persistence.

## skyrim-dev healthcheck

    PASS resource guard
    PASS git/worktree health
    PASS control plane
    PASS state persistence: /var/lib/skyrim-dev/state/state.json
    PASS product context

## roadmap-status and milestone-status

The applied and observed control-plane SHA is
3e7e893b4018b488e158aa5cda977399c0e55a75, status VALID, branch
orchestration/control-plane. M01 is ACTIVE; W01-W10 are
BLOCKED_EXTERNAL_GATE because the reviewed integration branch gate is
unresolved. M02-M05 are PLANNED and executable=false.

## Safety state at capture

    orchestrator: inactive, disabled
    healthcheck timer: inactive, disabled
    global mode: PAUSED
    development workers launched: no
    development branch heads changed: no

Protected development heads verified unchanged:

    combat:     905cf71c55200509702fb299aaa953ae46dcb374
    authority:  caf7dcc31ca4b6d0912f31b151408ba24c13938c
    population: 52c97ba4d5da993e6ef2fa4bdf398f13c22b456a
    ui:         a473531ad16a82cecc8a4cdecc460934ba7efcfa

The dedicated control-plane worktree is clean and checked out at the applied
branch. The only Git checkout changed by this pass was the review branch
infra/vds-orchestrator-review; no development lane branch was edited, pushed,
or merged.
