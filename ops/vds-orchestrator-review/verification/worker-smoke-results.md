# Worker sandbox smoke-test result

## Contract

`skyrim-dev worker-smoke-test` is a bounded diagnostic, not a development
phase. It runs as `skyrimdev` with the production worker settings in a unique
directory under `/var/lib/skyrim-dev/smoke/`. It:

- creates one harmless local input file;
- asks Codex to read it and conditionally create one harmless output file;
- uses `--sandbox workspace-write`, `--ephemeral`, and no Git repository;
- removes GitHub token variables, SSH-agent variables, and uses the empty
  worker `GH_CONFIG_DIR`;
- snapshots protected development branch/head/dirty state before and after;
- checks for a lingering process carrying the scratch path; and
- removes the disposable scratch directory in all normal completion paths.

It never calls Git, changes roadmap state, starts a worker lane, consumes a
phase, commits, or pushes.

## VDS result after architect-approved remediation

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

Before the profile was installed, the same command failed closed with
`SANDBOX_INFRA_BLOCKED`. After loading the official scoped profile, the exact
production smoke command passed. Both direct bwrap probes also returned `rc=0`
with empty output; see `sandbox-diagnosis.md`.

This result validates the worker sandbox only. It is not permission to start
autonomous development; the service remains inactive/disabled and the global
mode remains `PAUSED`.
