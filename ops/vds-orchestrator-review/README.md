# Skyrim Supervisor Roadmap / Dependency Control Plane review snapshot

## V3.2 bounded supervisor concurrency repair — 2026-09-22

The production VDS was paused before this repair. `advance_lane()` no longer
admits a `RECOVERING` worker; `schedule()` is the single admission authority
for `READY` and `RECOVERING` lanes, and `start_worker()` independently rejects
admission when the configured `max_concurrent_workers` cap is full. The cap
remains `2` and normal/recovery workers share the same slots.

The deterministic suite now passes 101 tests (baseline: 92), including the
resume-with-three-recoveries regression through the real
`run_once()`/`refresh_control()`/`advance_lane()`/`schedule()` path, direct
`start_worker()` defense-in-depth, slot refill, review gating, non-worker
states, rate-limit cap protection, and spawn-failure refill behavior. The
installed VDS source and tests match the review worktree; `self-test`,
`healthcheck`, and the disposable `worker-smoke-test` all pass. Production
remains globally paused with no development Codex worker. See
[verification/architect-review-20260922/v32-concurrency-repair.md](verification/architect-review-20260922/v32-concurrency-repair.md)
for the root cause, exact preserved lane snapshot, bounded worker log evidence,
and final service state.

This is the secret-free architect-review snapshot of Supervisor V3.1 with the
roadmap/dependency control plane, runtime-owner/worker-sandbox repair, and
daemon request durability repair. It was captured on 2026-09-22 after
installation and verification. Autonomous development remains disabled: the
orchestrator service is active in global `PAUSED` mode, the healthcheck timer
is active and enabled, and no worker is running.

## Installed layout

- Supervisor: `/srv/services/skyrim-dev/orchestrator/supervisor.py`
- Roadmap parser/scheduler: `/srv/services/skyrim-dev/orchestrator/roadmap.py`
- Unit tests: `/srv/services/skyrim-dev/orchestrator/test_supervisor.py` and
  `/srv/services/skyrim-dev/orchestrator/test_roadmap.py`
- Configuration: `/srv/services/skyrim-dev/config/supervisor.json`
- Management command: `/usr/local/bin/skyrim-dev`
- Product context: `/srv/services/skyrim-dev/product/`
- Persistent state: `/var/lib/skyrim-dev/state/state.json`
- Review packets: `/var/lib/skyrim-dev/review-packets/`
- Worker logs: `/var/log/skyrim-dev/`
- Empty worker GitHub config: `/var/lib/skyrim-dev/worker-gh-config/`
- Disposable worker smoke root: `/var/lib/skyrim-dev/smoke/`
- Control-plane worktree: `/srv/projects/skyrim-online-str/control-plane`
- Applied control-plane SHA: `3e7e893b4018b488e158aa5cda977399c0e55a75`
- Validated control cache: `/var/lib/skyrim-dev/state/control-plane/`

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
evaluation, so exactly three successes trigger the checkpoint. Roadmap control
commands are `roadmap-status`, `milestone-status`, `sync-control-plane`,
`approve-task`, `approve-control-plane`, and `accept-milestone`.

### Runtime-owner / observer separation

The daemon path acquires `/var/lib/skyrim-dev/state/supervisor.lock` before it
constructs `Supervisor(runtime_owner=True)`. Only that lock-owning daemon may
clear historical worker PIDs, convert persisted `CODING` lanes to recovery, or
reconcile a stale `RATE_LIMITED` probe. CLI, healthcheck, and self-test paths
construct the default observer (`runtime_owner=False`); their state writes are
disabled. Explicit operator commands enable only their own narrow mutation.

The deterministic regression suite exercises each observer command against
simulated live workers and asserts unchanged lane state, PID, timestamps,
recovery counters, worker metadata, scheduler ownership, and Codex probe state.
It also covers lock contention, legitimate daemon restart recovery, and an
operator command that must not reconcile an unrelated live lane.

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

## Product-aware scheduling

The dedicated control-plane worktree is synchronized every five minutes by the
trusted outer supervisor using the exact configured branch and fast-forward-only
Git. The canonical product files define the persistent multiplayer RP/MMO
vision, server-owned Character/AccountId/CharacterId authority, humanoid
suppression, trusted creature population, lifecycle/incarnation rules,
renewable encounters, and the first playable core-world milestone. Validated
copies are cached with the applied SHA; worker prompts receive only bounded
context plus the task identity and SHA.

The final captured C/A/L/U queues remain intact at C04, A04, L03, and U02.
They are normalized as `EXISTING_PLAN` tasks and remain review-gated. M01-WORLD W01-W10 are
`ROADMAP` tasks and remain visibly `BLOCKED_EXTERNAL_GATE` until the architect
provides the reviewed integration branch. M02-M05 are product direction only
with `executable: false` and can never be scheduled.

The scheduler persists dependency decisions and a bounded audit trail, uses
round-robin selection across ready lanes, caps concurrency at two, and keeps
Codex rate limiting global. Queue completion transitions M01 to runtime
acceptance waiting; it never accepts M01 automatically.

## Security boundary

Worker child processes receive no `GH_TOKEN`, `GITHUB_TOKEN`, enterprise token,
or `SSH_AUTH_SOCK`. `GH_CONFIG_DIR` is redirected to the empty,
service-owned `/var/lib/skyrim-dev/worker-gh-config`; the outer supervisor keeps
the real GitHub CLI configuration for its trusted push/CI operations. The
installed Codex CLI sandbox behavior was inspected on version `0.155.1`.

`skyrim-dev worker-smoke-test` is the only supported validation path for a
worker write. It uses a disposable scratch directory, local read/write proof,
no Git repository, no GitHub/SSH credentials, and cleanup after the process
exits. The architect-approved Ubuntu `bwrap-userns-restrict` AppArmor profile
is installed at `/etc/apparmor.d/bwrap-userns-restrict`, loaded/enforced for
`bwrap` and `unpriv_bwrap`, and leaves
`kernel.apparmor_restrict_unprivileged_userns=1` unchanged. Both direct bwrap
probes and the production smoke test now pass with `WORKER_SMOKE_OK`.

Residual risk: workers still run as the same `skyrimdev` Unix account and can
read any files that account can read, including Codex authentication needed for
worker operation. A filesystem/user separation was not improvised because it
would require a materially more complex privilege and authentication
architecture; this remains for architect review. Passing the sandbox gate does
not itself authorize autonomous development.

## Verification

- 92 deterministic unit tests passed, including the V3.1 request durability
  rollback, same-daemon retry, restart replay, duplicate suppression, and
  idempotent control-plane replay regressions.
- `skyrim-dev self-test` passed, including Codex/GitHub authentication checks,
  safe push dry-runs, four clean worktrees, product context, credential
  isolation, and GitHub Actions polling.
- `skyrim-dev healthcheck` passed resource, Git/worktree, state-persistence,
  control-plane, and canonical product-context checks.
- `skyrim-dev roadmap-status` passed with applied and observed SHA
  `3e7e893b4018b488e158aa5cda977399c0e55a75`; W01-W10 report the reviewed
  integration-branch external gate.
- `skyrim-dev milestone-status` passed and kept M01 `ACTIVE`; runtime evidence
  remains required.
- `skyrim-dev worker-smoke-test` passed with the explicit `WORKER_SMOKE_OK`
  result; both direct bwrap probes passed and no development worktree or branch
  changed.
- C04/A04/L03/U02 remain `NEEDS_SOL_REVIEW`; no lane decision was issued in
  this pass.
- UI readiness was verified from the repository workflow and the minimal
  supported Node 20/pnpm 9 tooling was installed; no UI dependencies were
  installed.
- No development worker was started, no development branch was pushed, and no
  development branch head changed.
- Protected development heads remain:
  `combat=500bf5ea5e04341f565776ed5263119c1cf06893`,
  `authority=b8fc40415fceee88ae6424d25bd68a2a0ddeb70a`,
  `population=52c97ba4d5da993e6ef2fa4bdf398f13c22b456a`,
  `ui=a473531ad16a82cecc8a4cdecc460934ba7efcfa`.
- The orchestrator remains active in global `PAUSED` mode, the healthcheck
  timer remains active and enabled, and no Luna worker was started.

See [CHANGELOG.md](CHANGELOG.md), [verification/self-test-results.md](verification/self-test-results.md),
[verification/security-scan.md](verification/security-scan.md),
[verification/control-plane-schema.md](verification/control-plane-schema.md),
[verification/first-run-blockers.md](verification/first-run-blockers.md),
[verification/sandbox-diagnosis.md](verification/sandbox-diagnosis.md),
[verification/worker-smoke-results.md](verification/worker-smoke-results.md),
[verification/ui-tooling-readiness.md](verification/ui-tooling-readiness.md), and
[state/persistent-lane-state.json](state/persistent-lane-state.json).

## V3 bounded repair — 2026-09-22

This section supersedes earlier V2/V2.1 installation-state prose above for the
bounded operator-request/validation repair. The review branch is installed on
the production VDS. The supervisor service is `active` in global `PAUSED`
mode; the healthcheck timer is `active/enabled`.

Mutating operator commands now use the daemon-owned durable request inbox at
`/var/lib/skyrim-dev/operator-requests`; observers remain read-only and there
is no offline `state.json` fallback. The processed-request ledger and durable
receipts provide restart-safe idempotence. See
[verification/operator-request-design.md](verification/operator-request-design.md).

Worker result semantics now include
`COMPLETE_WITH_VALIDATION_GAP` with a bounded `VALIDATION_GAP:` reason. The
supervisor still runs structural validation, focused checks when configured,
trusted commit, push, and exact-SHA CI. See
[verification/validation-gap.md](verification/validation-gap.md).

Prospective commits are checked with an isolated temporary Git index before the
real index is staged; the real cached diff check remains as defense in depth.
See [verification/prospective-diff-validation.md](verification/prospective-diff-validation.md).

The VDS suite passed 92 tests. The disposable Luna smoke test returned
`WORKER_SMOKE_OK` with the operator inbox probe `BLOCKED`. C04, A04, L03, and
U02 remain pending exactly as captured, with protected heads and dirty-worktree
bytes preserved. See [verification/final-verification.md](verification/final-verification.md),
[verification/current-state-redacted.json](verification/current-state-redacted.json),
and [verification/dirty-worktrees-v3.md](verification/dirty-worktrees-v3.md).

The V3.1 durability repair snapshots daemon state and the active roadmap before
dispatch. A failed durable save restores both snapshots, retains the inbox
request, emits no receipt, and stops the current request pass. The request is
then safe to replay in the same daemon or after restart. See
[verification/operator-request-durability.md](verification/operator-request-durability.md).

## Bounded architect-directed correction — 2026-09-22

The architect-directed recovery correction was applied only to the retained
production L03 and U02 worktrees while global mode remained `PAUSED`.
L03 now preserves the light namespace for every `.esl` file, promotes
ESL-flagged `.esp` and `.esm` files, and falls back conservatively
for malformed headers. Its focused tests include malformed TES4 headers and
metadata-only loading. U02 has the accepted EOF and historical recovery-text
corrections, with both working-tree and cached diff checks passing. No lane
decision or development HEAD changed. Full evidence is in
[verification/architect-review-20260922/bounded-correction-20260922.md](verification/architect-review-20260922/bounded-correction-20260922.md).

## 2026-09-23 worker model and recovery verification

The installed supervisor and the infra branch source now agree on the two-worker recovery cap. Future development workers use gpt-6-luna with maximum reasoning; the configured concurrency remains two. The Population temporary-index validation now passes under the supervisor identity without changing its real index or retained files.

The three exact checkpoint approvals and the guarded Population retry completed while paused. After Phase 8 passed, a five-minute observation recorded no more than two workers. The 20 root-owned loose-object fan-out directories were corrected to the repository's skyrimdev owner, and the Population temporary-index check passes without changing its real index. In the 01:38 capture, GLOBAL was RUNNING with a valid control plane; Combat C05 and Population L03 awaited Sol review, UI U03 awaited Sol review after passing CI, and Authority A06 was retrying after a Linux CI failure with one live GPT-6 Luna/max worker. No new review packet was approved after resume.

See [verification/phase5-10-recovery.md](verification/phase5-10-recovery.md) for source paths, hashes, tests, permission evidence, lane decisions, worker command evidence, and final worktree status.


## Isolated Sol architect review — 2026-09-23

The supervisor now runs one serialized `gpt-6-sol/max` architect review alongside
at most two Luna development workers. It builds an immutable, secret-redacted
bundle for the exact lane, phase, worktree diff, validation, CI, product context,
roadmap dependencies, and cross-lane interfaces, then sends that bounded bundle
inline to the reviewer. The reviewer receives no file, shell, MCP, browser,
plugin, or network tools. Codex MCP is disabled per invocation, the CLI uses
`approval_policy=never` with `read-only` sandboxing, and the supervisor rejects
the result if the JSONL event stream contains any non-text tool item. The
outer Bubblewrap boundary also hides supervisor state and inbox, worker trees,
service files, logs, and host credentials.

Responses use a strict JSON schema and must match the exact review ID, lane,
phase, worktree SHA, and state digest. The supervisor rechecks that identity
before applying any decision. Exact-SHA CI and dependency gates remain enforced;
review actions are recovery guidance only. A current-phase approval cannot
advance a phase, and milestone/runtime acceptance remains human-owned. Review
retries, repeated findings, CI repairs, failures, and backoff all have explicit
bounds. `skyrim-dev review-status` shows the queue, while
`skyrim-dev architect-review-smoke-test` runs the disposable Bubblewrap and real
Sol-model smoke check.


## 24/7 autonomous review verification — 2026-09-23

This deployment supersedes the earlier paused-mode status captured above. The
supervisor is active and enabled, and autonomous development is running with a
maximum of two `gpt-6-luna`/max workers plus one independent serialized
`gpt-6-sol`/max review process. Review identity excludes scheduler-only
`evaluated_at` fields from both the state digest and immutable evidence bundle;
a versioned identity prevents collisions with bundles written by the earlier
format. Each tick coalesces superseded queued review items.

The RETRY gate requires concrete bounded actions and remaining budget; high
findings can be repaired in the same phase, while low-confidence/actionless
RETRY and any critical/high APPROVE remain blocked. The earlier policy's
actionable RETRY is rechecked against the exact saved bundle and current lane
state before it can resume.

The final verification record contains the exact control-plane SHA, service and
worker state, lane HEAD/CI status, reviewer queue, test results, and deployed
source hashes. M01 runtime acceptance remains human-gated.
See [verification/final-verification.md](verification/final-verification.md).
