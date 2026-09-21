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

The future `RoadmapTask` adapter in `roadmap.py` is intentionally small. It
maps the current authoritative PLAN.md record to the future scheduler contract
without replacing the current queues or introducing a dependency graph in V2.

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
