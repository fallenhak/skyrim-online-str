# Skyrim Supervisor Hardening V2.1 review snapshot

This is the secret-free architect-review snapshot of the V2.1 supervisor installed
on the Ubuntu VDS. It was captured on 2026-09-21 after installation and
verification. Autonomous development remains disabled: the orchestrator unit
and healthcheck timer are both inactive and disabled, global mode is `PAUSED`,
and no worker was launched during this pass.

## Installed layout

- Supervisor: `/srv/services/skyrim-dev/orchestrator/supervisor.py`
- Future task adapter: `/srv/services/skyrim-dev/orchestrator/roadmap.py`
- Unit tests: `/srv/services/skyrim-dev/orchestrator/test_supervisor.py`
- Configuration: `/srv/services/skyrim-dev/config/supervisor.json`
- Management command: `/usr/local/bin/skyrim-dev`
- Product context: `/srv/services/skyrim-dev/product/`
- Persistent state: `/var/lib/skyrim-dev/state/state.json`
- Review packets: `/var/lib/skyrim-dev/review-packets/`
- Worker logs: `/var/log/skyrim-dev/`
- Empty worker GitHub config: `/var/lib/skyrim-dev/worker-gh-config/`

The review branch contains the corresponding source under `source/`, a redacted
state projection under `state/`, and verification evidence under `verification/`.

## V2/V2.1 safety behavior

### CI aggregation

The configured required workflows are `Build linux` and `Build windows`. CI is
queried for the exact pushed commit SHA, grouped by workflow name, and reduced
to the newest relevant attempt per workflow. The lane waits for every required
workflow to appear and complete. A phase becomes CI-green only when every
required workflow concludes `success`; any required failure fails the phase.
The per-workflow run ID, SHA, status, conclusion, URL, and bounded failure
excerpt are persisted and included in review packets. Appearance and overall
completion waits remain bounded.

### Review lifecycle

Review metadata is explicit and persistent:

- `POST_PHASE_CHECKPOINT`: the current phase completed and passed all required
  CI. `skyrim-dev approve <lane>` advances exactly once to the recorded next
  phase and resets `success_since_review`; it never reruns the completed phase.
- `CURRENT_PHASE_REVIEW`: the current phase is not safely complete. `approve`
  refuses; `skyrim-dev retry <lane>` is required to retry/continue that phase.
- `FINAL_MILESTONE_OR_QUEUE_REVIEW`: no automatic next work exists. Approval
  records a safe terminal decision and leaves the lane `BLOCKED`.

`skyrim-dev review-status`, `approve`, `retry`, and `block` are explicit
operator interfaces. The third successful phase is counted before checkpoint
evaluation, so exactly three successes trigger the checkpoint.

For `CURRENT_PHASE_REVIEW`, `retry` always enters `RECOVERING` while the global
mode is `RUNNING`. While globally paused it records
`paused_from_state=RECOVERING`, then restores `RECOVERING` on resume even when
the worktree is already clean. The recovery worker is told explicitly that it
is correcting the same failed phase.

### V2.1 Git fail-closed and review bounds

There is one `git_dirty()` implementation, and unreadable Git status is always
dirty/unsafe. `git_status_files()` returns an explicit failure (`None`) instead
of silently producing an empty clean file list; pre-worker, post-commit, and
pre-push paths therefore stop for human review when status cannot be read.

Reasonable-size textual untracked files are rendered as bounded new-file diffs
and included in risk analysis. Binary, unreadable, symlink-escaping, and
oversize files contribute their real size to bounds, expose metadata only, and
require review without dumping their contents into logs or prompts. The total
review text is capped by the configured 512 KiB bound.

Recovery prompts now contain bounded, redacted `RECOVERY CONTEXT` with the last
failure, required CI failure excerpt, and relevant worker evidence. Normal
worker prompts do not include persisted recovery failures, and workers are
explicitly told not to query GitHub.

### Git and worker safety

Git state is read with `git status --porcelain=v1 -z --untracked-files=all` and
parses staged, unstaged, untracked, deleted, renamed, and unmerged entries.
Both staged and unstaged tracked diffs are reviewed. Before every worker, the
expected branch and status are verified; normal workers require a clean
worktree, while only a bounded recovery worker may inspect a dirty worktree.
The outer supervisor stages only the validated explicit file list, never
`git add -A`, and verifies cleanliness after its commit. Root-level and nested
generated paths (`build`, `node_modules`, `dist`, `out`, `.xmake`, and `obj`)
are rejected. Changed-file and total-diff-byte bounds default to 40 files and
512 KiB. A `COMPLETE` result with no reviewable source/test/documentation
changes goes to `NEEDS_SOL_REVIEW`.

Worker logs are unique per invocation, for example
`combat-C03-attempt-1-<timestamp>.log`. The active path is persisted and
`WORKER_RESULT` is parsed only from that path. Retention is bounded to 20 logs
per lane.

### Pause and Codex availability

Operator pause and automatic resource/Git-health safety pause stop active
workers, preserve dirty diffs, and prevent all new commit, push, or phase
advancement mutations. The chosen behavior is terminate-and-freeze; workers do
not finish into Git mutation after pause. Safety pause resumption requires the
resource/Git condition to clear.

Codex/ChatGPT allowance exhaustion is classified separately from engineering
failure using conservative provider-context patterns. It does not consume
phase recovery or CI failure budgets. Persistent global state records
`AVAILABLE`/`RATE_LIMITED`, detection time, bounded backoff, retry time, and
reason. The first retry is 15 minutes, then 30, then at most 60 minutes; only
one pending worker is probed at a retry time. A successful probe restores normal
scheduling automatically. `skyrim-dev status` exposes this state as `CODEX:`.

### Validation evidence

Structural checks, configured focused tests, and GitHub CI are recorded as
separate evidence. No focused command is invented: when none is configured the
state and review packet say `focused tests: not independently verified by
supervisor`. A command is reported passed only when the supervisor observed its
exit result.

## Product awareness

The installed product files define the persistent multiplayer RP/MMO vision,
server-owned Character/AccountId/CharacterId authority, humanoid suppression,
trusted creature population, lifecycle/incarnation rules, renewable encounters,
and the first playable core-world milestone. Worker prompts receive a bounded
vision/rules/milestone section plus lane-specific relevance, the current phase,
hard boundaries, and a roadmap task record. The current C/A/L/U queues remain
unchanged; `roadmap.py` is only an adapter for a future milestone/dependency
scheduler.

## Security boundary

Worker child processes receive no `GH_TOKEN`, `GITHUB_TOKEN`, enterprise token,
or `SSH_AUTH_SOCK`. `GH_CONFIG_DIR` is redirected to the empty,
service-owned `/var/lib/skyrim-dev/worker-gh-config`; the outer supervisor keeps
the real GitHub CLI configuration for its trusted push/CI operations. The
installed Codex CLI sandbox behavior was inspected on version `0.155.1`.

Residual risk: workers still run as the same `skyrimdev` Unix account and can
read any files that account can read, including Codex authentication needed for
worker operation. A filesystem/user separation was not improvised because it
would require a materially more complex privilege and authentication
architecture; this remains for architect review.

## Verification

- 31 supervisor unit tests passed under `skyrimdev`, including deterministic
  V2.1 cases for Git-status failure, restart-safe rate-limit probes, retry
  semantics, recovery evidence, and untracked-file review bounds.
- `skyrim-dev self-test` passed, including Codex/GitHub authentication checks,
  safe push dry-runs, four clean worktrees, product context, credential
  isolation, and GitHub Actions polling.
- `skyrim-dev healthcheck` passed resource, Git/worktree, state-persistence,
  and product-context checks.
- No development worker was started, no development branch was pushed, and no
  development branch head changed.
- Protected development heads remain:
  `combat=905cf71c55200509702fb299aaa953ae46dcb374`,
  `authority=caf7dcc31ca4b6d0912f31b151408ba24c13938c`,
  `population=52c97ba4d5da993e6ef2fa4bdf398f13c22b456a`,
  `ui=a473531ad16a82cecc8a4cdecc460934ba7efcfa`.

See [CHANGELOG.md](CHANGELOG.md), [verification/self-test-results.md](verification/self-test-results.md),
[verification/security-scan.md](verification/security-scan.md), and
[state/persistent-lane-state.json](state/persistent-lane-state.json).
