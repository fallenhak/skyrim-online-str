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

## VDS result

    worker local filesystem read: FAIL
    worker local allowed write: FAIL
    bwrap RTM_NEWADDR error: ABSENT (direct probe: PRESENT)
    sandbox failure signal: PRESENT
    GitHub credentials exposed: NO
    SSH agent exposed: NO
    development worktree modified: NO
    development branch changed: NO
    persistent worker process: NO
    WORKER_SMOKE_FAILED: SANDBOX_INFRA_BLOCKED: scratch read/write proof failed; Codex workspace-write sandbox reported a bwrap/loopback failure

The exact CLI evidence was a workspace command-runner failure during the
allowed file operation. The direct bwrap probe and kernel audit identify the
underlying `RTM_NEWADDR`/namespace denial; see `sandbox-diagnosis.md`.

Because local read/write proof did not pass, this result is not approval to
start autonomous development. The service remains inactive/disabled and the
global mode remains `PAUSED`.
