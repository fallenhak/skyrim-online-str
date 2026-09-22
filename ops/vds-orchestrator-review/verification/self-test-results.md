# Supervisor repair verification results

Captured on the VDS on 2026-09-22. The orchestrator service remained active in
global `PAUSED` mode and the healthcheck timer remained active and enabled. No
development worker was started.

## Unit tests

Command:

    cd /srv/services/skyrim-dev/orchestrator
    runuser -u skyrimdev -- env HOME=/home/skyrimdev PYTHONPATH=/srv/services/skyrim-dev/orchestrator PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s . -p 'test_*.py' -v

Result: 92 tests passed. This includes the prior roadmap/supervisor tests and
new deterministic coverage for:

- status, roadmap-status, milestone-status, review-status, healthcheck, and
  self-test observer ownership preservation;
- explicit operator non-reconciliation;
- runtime-owner restart reconciliation and daemon lock ordering;
- live `RATE_LIMITED` probe preservation versus legitimate restart cleanup;
- local worktree source-access prompt policy;
- worker GitHub/SSH environment isolation; and
- disposable worker smoke-command construction;
- failed operator-request save rollback and inbox retention;
- same-daemon retry and restart replay from the last persisted state;
- duplicate suppression after a successful save; and
- idempotent `sync-control-plane` replay after a pre-commit failure.

## Observer and runtime-owner behavior

The observer fixture persisted simulated live workers in combat/authority and
verified unchanged `CODING` state, worker PIDs, timestamps, recovery counters,
worker metadata, scheduler ownership, and Codex probe state after every
observer path. The lock regression patches a held lock and verifies that the
second daemon returns before constructing a runtime owner. A separate restart
fixture verifies that a real runtime owner still converts stale `CODING` state
to recovery and clears its historical PID.

## Existing production checks

The prior V2.1 checks remain required and were rerun after installation:

- `skyrim-dev self-test`: `SELF_TEST_OK`.
- `skyrim-dev healthcheck`: resource guard, Git/worktree health, control plane,
  state persistence, and product context all `PASS`.
- `skyrim-dev status`, `roadmap-status`, `milestone-status`, and
  `review-status`: completed while the service was paused; no runtime-owner
  reconciliation occurred.
- Observer state hash was unchanged across all six commands:
  `b20eeff5a315cb09524d6e79a801d058b45e1328ffb445492f277f2640e2ab3a`
  before and after (`OBSERVER_STATE_HASH_UNCHANGED`).
- Control plane remains `VALID` at
  `3e7e893b4018b488e158aa5cda977399c0e55a75`.

## Worker smoke test

Command:

    skyrim-dev worker-smoke-test

Result after architect-approved AppArmor remediation:

    worker local filesystem read: PASS
    worker local allowed write: PASS
    bwrap RTM_NEWADDR error: ABSENT
    sandbox failure signal: ABSENT
    GitHub credentials exposed: NO
    SSH agent exposed: NO
    development worktree modified: NO
    development branch changed: NO
    persistent worker process: NO
    WORKER_SMOKE_OK

The installed Codex CLI read and wrote only the disposable scratch workspace.
No lane, roadmap, branch, Git metadata, or persistent worker process was
affected. The scratch directory was removed after the test.

## Sandbox remediation evidence

- Package versions: `apparmor`, `apparmor-profiles`, and `apparmor-utils`
  `4.0.1really4.0.1-0ubuntu0.24.04.7`; `bubblewrap`
  `0.9.0-1ubuntu0.3`.
- Official source profile existed at
  `/usr/share/apparmor/extra-profiles/bwrap-userns-restrict` and was installed
  at `/etc/apparmor.d/bwrap-userns-restrict` with mode `0644 root:root`.
- Profile SHA-256:
  `11d39094f044f0cda0febb3ad517b830301da6b2ce929664af09ee9e4dd264f9`.
- `apparmor_parser -r` succeeded; `apparmor_parser -Q -T` returned `0`;
  kernel profiles `bwrap (enforce)` and `unpriv_bwrap (enforce)` are present.
- `kernel.apparmor_restrict_unprivileged_userns=1` and
  `kernel.unprivileged_userns_clone=1` remained unchanged.
- Direct `bwrap --unshare-user` and `--unshare-net` probes both returned
  `rc=0` with no output.
- Worker options remain `--sandbox workspace-write`,
  `approval_policy="never"`, and `--ephemeral`; GitHub/SSH variables were
  absent and `GH_CONFIG_DIR` remained `/var/lib/skyrim-dev/worker-gh-config`.

## First-run evidence preserved from before remediation

- C03 complete-log result: no actual `WORKER_RESULT` marker was emitted; the
  apparent marker is part of the worker prompt template. The supervisor log
  records operator pause and worker termination at `2026-09-21T18:19:22Z`.
  The last technical evidence is the sandbox/tool-router failure. See
  `verification/first-run-blockers.md`.
- A04 complete-log result: the worker identified the concrete
  `DrawWeaponRequest`/`OwnershipEpoch` stale same-client reacquisition risk,
  then failed to read/write the local file because of the same bwrap loopback
  failure. See `verification/first-run-blockers.md`.

## Protected state

    global mode: PAUSED
    orchestrator: active / enabled
    healthcheck timer: active / enabled
    development workers: none

Protected heads remain unchanged:

    combat:     500bf5ea5e04341f565776ed5263119c1cf06893
    authority:  b8fc40415fceee88ae6424d25bd68a2a0ddeb70a
    population: 52c97ba4d5da993e6ef2fa4bdf398f13c22b456a
    ui:         a473531ad16a82cecc8a4cdecc460934ba7efcfa

Autonomous development remains disabled pending architect review and a safe,
bounded Codex workspace-write remediation.

## V3 final capture

The installed self-test returned `SELF_TEST_OK` after explicitly accepting only
the conflict-free dirty worktrees whose lane state is
`CURRENT_PHASE_REVIEW`; authority remained clean. It also passed the operator
inbox location/permission check and the negative worker-inbox contract. The
full final command set is summarized in [final-verification.md](final-verification.md).
The V3.1 durability regression suite passed 92 tests and proves failed saves
cannot leave an in-memory processed marker eligible for receipt/archive replay.
