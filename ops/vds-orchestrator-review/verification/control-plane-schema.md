# Control-plane schema and scheduler contract

The supervisor reads .orchestrator/roadmap.json only from the dedicated
orchestration/control-plane worktree. The worktree is an operational
read-only input; the trusted outer supervisor performs fetch, branch/status
checks, and fast-forward-only synchronization.

## Required shape

- version: positive integer.
- current_milestone: ID present in milestones.
- scheduler_policy: object containing scheduling policy.
- milestones: list of unique IDs, titles, valid milestone states, executable
  flag, workstreams, and acceptance criteria.
- workstreams: each has a unique ID, lane, and mode of EXISTING_PLAN or
  ROADMAP_TASKS. Existing queues require start_phase. A future/nonexistent
  lane must be explicit (branch: null and a blocked/planned status).
- roadmap tasks: unique ID, title, inherited lane context, risk,
  requires_sol_review, depends_on, and acceptance criteria.
- external_gates and blocked_by are explicit strings and are never satisfied
  by file existence or intuition.

The parser rejects duplicate IDs, missing task context, unknown dependencies,
self-dependencies, cycles, invalid states, malformed future lanes, and
executable tasks under executable: false milestones. It fails closed rather
than substituting a default roadmap.

## Persisted task identity

Every existing PLAN phase and roadmap-native task is normalized to:

milestone_id, workstream_id, task_id, lane, title, source, dependencies,
risk, requires_sol_review, acceptance_criteria, and the applied control-plane
SHA.

source is EXISTING_PLAN for C/A/L/U phases and ROADMAP for W01-W10. The
identity and a definition hash are persisted in lane state, worker prompts,
commit/CI history, audit records, and review packets.

## State and gates

The scheduler persists READY, RUNNING, BLOCKED_DEPENDENCY,
BLOCKED_EXTERNAL_GATE, NEEDS_SOL_REVIEW, PAUSED, and DONE. A task is ready only
after milestone activation, dependency completion, lane sequencing,
external-gate resolution, exact task approval where required, control-plane
validation, and lane availability.

Roadmap tasks with requires_sol_review: true create a
PRE_TASK_ROADMAP_REVIEW packet before implementation. approve-task records the
exact task definition hash and exact control-plane SHA; either changing
invalidates the approval.

The active roadmap is retained when a fetched candidate is malformed. Changes
to active/running work, completed semantics, running-work dependencies,
WORLD_RULES.md, or milestone acceptance semantics enter NEEDS_SOL_REVIEW;
only future/unstarted changes may auto-apply.

## Milestone acceptance

Engineering completion transitions M01 to WAITING_RUNTIME_ACCEPTANCE; it does
not accept the product. accept-milestone M01 --evidence-file <json> requires
human-reviewed Windows Skyrim evidence for two clients, Character Select,
humanoid suppression, synchronized creature encounters, clear/reset/re-entry,
stale-incarnation rejection, and reconnect restoration. Linux checks never
pretend to be runtime acceptance.
