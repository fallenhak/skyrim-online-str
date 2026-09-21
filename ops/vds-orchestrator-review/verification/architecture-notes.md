# V2.1 architecture notes

The current supervisor remains a deterministic outer state machine around the
four existing C/A/L/U queues. It owns branch verification, worker lifecycle,
validation, Git mutation, push, CI observation, review checkpoints, resource
pauses, and persistent global Codex availability. Workers only edit ordinary
source/test/documentation files and report one result marker.

CI is an aggregation boundary, not a single-run latch: the exact commit SHA is
the key, required workflow names are the grouping key, and the newest relevant
attempt for each workflow is the only result considered. Review packets carry
the complete aggregation so a green workflow cannot hide a missing or failed
workflow.

Review approval is a state-machine action, not an inferred side effect. A
post-phase checkpoint retains the completed phase and its recorded next phase
until explicit approval. A current-phase failure can only be retried. A final
queue review has no automatic next work and therefore resolves to a safe
blocked state.

The control-plane scheduler in `roadmap.py` now validates the canonical
roadmap, normalizes both existing PLAN phases and roadmap-native work into a
common task identity, evaluates dependencies fail-closed, and persists each
decision. The existing queues are wrapped, not replaced; their persisted
phase indexes remain authoritative.

Product context is injected as a bounded prompt section because the reason for
a phase is part of its safety boundary. The canonical files remain the source
of truth; prompts carry only the relevant vision, immutable rules, milestone,
lane rationale, task metadata, and plan boundaries.

## V2.1 corrections

Git status has one fail-closed path: `git_status_details()` preserves command
failure, `git_dirty()` treats it as dirty, and `git_status_files()` returns an
explicit non-clean result instead of an empty list. The same condition blocks
worker admission, post-commit progression, and push. The duplicate older helper
was removed.

Startup runtime markers are reconciled against this supervisor's in-memory
process ownership. A persisted rate-limit probe with no live supervisor-owned
process is cleared, while a future retry remains unchanged or a bounded retry
is scheduled using the existing backoff. Lane recovery counters are not changed
by this reconciliation.

`CURRENT_PHASE_REVIEW` is an incomplete-phase state. An operator retry always
selects `RECOVERING`; if paused, `paused_from_state` records that state so a
later resume cannot infer `READY` from a clean worktree. Recovery prompts carry
bounded, redacted failure context from persisted error, CI, and worker evidence.

Untracked text is represented with a bounded new-file diff before risk scanning,
so security-sensitive content such as `CharacterId` cannot be hidden by a
generic filename. Binary, oversize, unreadable, and unsafe-path entries expose
metadata only and route to review; actual untracked bytes still count toward
the total diff bound.

## Roadmap / dependency control plane

The trusted outer supervisor owns the dedicated
`/srv/projects/skyrim-online-str/control-plane` worktree. It fetches only
`orchestration/control-plane`, requires a clean exact-branch worktree, and
fast-forwards it without force or rewrite. The candidate is parsed before it
can become active. Invalid candidates preserve the last valid cached snapshot;
changes to active work, completed semantics, running-work dependencies,
`WORLD_RULES.md`, or milestone acceptance semantics create a global
`NEEDS_SOL_REVIEW` gate.

M01-WORLD W01-W10 remain blocked by the canonical unresolved reviewed
integration-branch gate. The parser allows this explicit future lane with
`branch: null`, but the scheduler never creates a branch or worker for it.
M02-M05 have `executable: false` and are never expanded or scheduled.

Task approvals record both a definition hash and control-plane SHA. The audit
history records control application, selection, dependency evaluation, task
start/completion, reviews, approvals, and milestone transitions with bounded
retention. Round-robin lane selection persists its cursor and respects the
existing two-worker limit and global rate-limit pause behavior.

M01 engineering completion is a separate state transition to
`WAITING_RUNTIME_ACCEPTANCE`. Only an explicit operator acceptance with
reviewed Windows Skyrim evidence can reach `ACCEPTED`.

## Runtime owner and observer separation

The constructor now takes an explicit `runtime_owner` flag. The default
`runtime_owner=False` path is used by status, roadmap, milestone, review,
healthcheck, self-test, and explicit operator commands. It may normalize an
in-memory view for output, but it never performs startup reconciliation and
`save_state()` is disabled unless an operator command explicitly opts into its
narrow mutation.

The daemon uses a separate `run_daemon()` path. It opens and non-blockingly
locks `supervisor.lock` first, then constructs `Supervisor(runtime_owner=True)`.
Only the lock owner may clear historical worker PIDs, convert persisted
`CODING` lanes to `RECOVERING`/`PAUSED`, or reconcile stale rate-limit probe
markers. A second daemon exits before constructing a Supervisor, so it cannot
reinterpret live ownership. This preserves legitimate restart recovery without
making observers destructive.

The worker prompt also declares the local lane worktree authoritative and
forbids GitHub connector/web-search source retrieval when the source is already
checked out. The trusted outer supervisor remains the only process allowed to
mutate Git metadata or push.

## Sandbox boundary

`worker-smoke-test` invokes the installed Codex CLI as `skyrimdev` with the
production worker settings in a unique disposable directory. It proves local
read/write, checks the sanitized GitHub/SSH environment, compares protected
lane branch/head/dirty snapshots before and after, checks for a lingering
process, and removes the scratch directory. It does not start the orchestrator
or consume scheduler state.

The architect-approved remediation installs Ubuntu's scoped
`bwrap-userns-restrict` profile and loads it in enforced mode. It preserves
global AppArmor and `kernel.apparmor_restrict_unprivileged_userns=1`; it does
not set capabilities on bubblewrap or fall back to `danger-full-access`. Direct
user/network probes and the production smoke test pass after this bounded
profile is loaded. Passing this infrastructure gate does not start or
authorize autonomous development.
