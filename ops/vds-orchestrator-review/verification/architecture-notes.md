# V2 architecture notes

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
