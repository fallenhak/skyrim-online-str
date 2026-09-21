# VDS orchestrator review snapshot

This directory is a secret-free snapshot of the files currently installed on the Ubuntu VDS for the Skyrim development-lane supervisor. It is for architect review only. It does not enable the supervisor, start a worker, alter a development branch, or merge anything.

Captured: `2026-09-21T14:26:49+00:00`  Repository: `fallenhak/skyrim-online-str`  Review branch: `infra/vds-orchestrator-review`

## Installed paths and ownership

- Python supervisor: `/srv/services/skyrim-dev/orchestrator/supervisor.py`
- JSON configuration: `/srv/services/skyrim-dev/config/supervisor.json`
- Management command: `/usr/local/bin/skyrim-dev`
- Persistent state: `/var/lib/skyrim-dev/state/state.json`
- Operator control file: `/var/lib/skyrim-dev/state/control.json`
- Human-review packets: `/var/lib/skyrim-dev/review-packets/`
- Runtime logs: `/var/log/skyrim-dev/`
- systemd units: `/etc/systemd/system/skyrim-dev-orchestrator.service`, `/etc/systemd/system/skyrim-dev-healthcheck.service`, `/etc/systemd/system/skyrim-dev-healthcheck.timer`
- Log rotation: `/etc/logrotate.d/skyrim-dev`

The orchestrator and healthcheck units run as `skyrimdev:skyrimdev`, with `NoNewPrivileges=yes`, `PrivateTmp=yes`, and a restricted PATH. The management command runs read-only status/health/self-test operations as `skyrimdev`; start/resume and stop require root because they control systemd. The service account has no sudo membership.

The installed orchestrator directory contained exactly one local Python module: `supervisor.py`. Its standard-library imports are self-contained; there were no additional installed project Python modules or templates to copy. The review tree intentionally excludes `/home/skyrimdev/.codex/auth.json`, `/home/skyrimdev/.config/gh/hosts.yml`, all SSH directories/keys, credential-bearing environment files, cookies, command history, conversation logs, and runtime logs.

## How one phase is chosen

For each lane, the supervisor parses the authoritative `PLAN.md` queue into ordered phase records and selects the record at the persisted zero-based `phase_index`. The four lanes are scheduled in deterministic order: combat, authority, population, ui. The persisted architect checkpoint in this snapshot is:

- combat: `C03`
- authority: `A04`
- population: `L03`
- ui: `U02`

All four lanes are currently `PAUSED`; no worker PID is active. The snapshot preserves the last commit recorded for each lane and does not claim any new development work was performed by this bootstrap.

## How one worker is invoked

When explicitly started, the outer supervisor allows at most two workers concurrently. A worker is invoked with the installed Codex CLI using the equivalent command shape:

```text
codex exec --model gpt-5.6-luna --config model_reasoning_effort="max" --config approval_policy="never" --sandbox workspace-write --cd <lane-worktree> --ephemeral --color never <one-phase prompt>
```

The prompt requires exactly one phase, prohibits edits to plans, service files, credentials, Git metadata, or unrelated lanes, and prohibits `git add`, `commit`, `push`, reset, clean, merge, rebase, checkout, and switch. The supervisor removes API-key, Codex-token, GitHub-token, and SSH-agent variables from the child environment. Worker output is bounded and secret-redacted before it is logged.

## How the outer supervisor commits and pushes

After the worker exits with `WORKER_RESULT: COMPLETE`, the supervisor verifies the expected lane branch, collects the diff, rejects protected/cross-lane/credential-like/generated paths, runs `git diff --check`, and then performs the Git add and commit itself. It pushes only the configured lane branch with a normal non-force push. A no-change phase advances without creating an empty commit.

## How CI is polled

After a push, the supervisor uses `gh run list --commit <exact-sha>` and accepts only a matching `headSha`. It waits up to 10 minutes for a run to appear and up to 30 minutes for completion. Failed output is bounded to the last 400 lines. Repeated polling failure (three attempts) or a CI timeout stops the lane for human review.

## Recovery rules

A worker is stopped after 90 minutes total or 20 minutes without output progress. One bounded recovery worker may retry the same phase without discarding/resetting a dirty diff. A second worker failure, a second CI failure for the same phase, unsafe local validation, a changed lane branch, missing plan, or repeated API failure leads to `NEEDS_SOL_REVIEW`. Resource guards automatically pause before work when root filesystem free space is below 8 GiB or available memory is below either 1 GiB or 10%; a periodic Git/worktree health check also pauses on failure. A process lock prevents a second supervisor instance.

## Sol checkpoint rules

The supervisor stops for review after three successful phases since the last review, or when a change touches persistent schema/migration/database surfaces, `AccountId`/`CharacterId` authority, XP/experience/reward/loot vocabulary, protocol/encoding paths, a configured forbidden lane boundary, or a possible trust-boundary relaxation. It writes a bounded review packet and, when configured, comments on the mapped GitHub issue. It never resumes a review-stopped lane without an explicit operator decision.

## Reboot behavior

The unit is currently **inactive and disabled**, so this snapshot does not create 24/7 operation and no reboot will start it automatically. If an operator later enables and starts the unit, systemd uses `Restart=on-failure`; on supervisor startup, a persisted `CODING` lane is converted to `RECOVERING` only when global mode is `RUNNING`, otherwise it is safely changed to `PAUSED`, preserving the diff for review. The control/state files are atomically written under `/var/lib/skyrim-dev/state/`.

## Concurrency

`max_concurrent_workers` is `2`. Scheduling walks the fixed lane order and starts only `READY` or bounded `RECOVERING` lanes. Each worker is confined to its own configured worktree; the trusted outer supervisor owns validation, commit, push, CI polling, and phase advancement.

## Captured branch heads

- authority: `caf7dcc31ca4b6d0912f31b151408ba24c13938c`
- combat: `905cf71c55200509702fb299aaa953ae46dcb374`
- population: `52c97ba4d5da993e6ef2fa4bdf398f13c22b456a`
- ui: `a473531ad16a82cecc8a4cdecc460934ba7efcfa`

The full redacted state projection is [state/persistent-lane-state.json](state/persistent-lane-state.json). The source-to-installed-path mapping and function map are in [verification/source-map.md](verification/source-map.md). This review snapshot was captured while autonomous development remained disabled.
