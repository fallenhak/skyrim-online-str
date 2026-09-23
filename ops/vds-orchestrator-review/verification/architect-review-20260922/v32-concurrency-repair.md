# V3.2 bounded supervisor concurrency repair — 2026-09-22

This is the secret-free receipt for the architect-directed production repair.
Only the `infra/vds-orchestrator-review` review branch and the installed
supervisor source were changed. Development lane state and bytes were treated
as preserved recovery state.

## Production guard and incident snapshot

The first production action was:

```text
skyrim-dev pause
```

The resulting guard was:

```text
GLOBAL: PAUSED (operator requested pause)
orchestrator.service: active/enabled
skyrim-dev-healthcheck.timer: active/enabled
control plane: VALID
development Codex workers: none
```

The production state after pause recorded the three resumed recovery workers
as terminated by the normal pause path:

```text
combat     state=PAUSED paused_from_state=CODING worker_pid=none
           last_error=worker exit=-15; marker=missing
population state=PAUSED paused_from_state=CODING worker_pid=none
           last_error=worker exit=-15; marker=missing
ui         state=PAUSED paused_from_state=CODING worker_pid=none
           last_error=worker exit=-15; marker=missing
authority  state=PAUSED paused_from_state=READY worker_pid=none
           last_error=none
```

The pause path preserved these exact development branches, heads, and status:

```text
combat
  branch: parallel/combat-foundations
  HEAD: 500bf5ea5e04341f565776ed5263119c1cf06893
  status:
    ?? Code/server/Services/ValidatedHitObservation.h
    ?? Code/tests/ValidatedHitObservationTests.cpp

authority
  branch: parallel/interaction-authority
  HEAD: b8fc40415fceee88ae6424d25bd68a2a0ddeb70a
  status: clean

population
  branch: parallel/population-loader
  HEAD: 52c97ba4d5da993e6ef2fa4bdf398f13c22b456a
  status:
     M Code/components/es_loader/ESLoader.cpp
     M Code/components/es_loader/Records/Record.h
     M Code/components/es_loader/TESFile.cpp
     M Code/components/es_loader/TESFile.h
     M Code/tests/ActorPopulationTests.cpp
     M docs/ACTOR_POPULATION.md

ui
  branch: parallel/character-ui
  HEAD: a473531ad16a82cecc8a4cdecc460934ba7efcfa
  status:
    M  docs/CHARACTER_UI_AUDIT.md
    A  docs/CHARACTER_UI_STATE_MACHINE.md
```

Authority's A04 worker completed naturally before the later pause and its
bounded log ended with the stale-incarnation fix summary and
`WORKER_RESULT: COMPLETE`; the supervisor had already recorded its review
checkpoint. Combat, population, and UI did not complete naturally in the
three-worker window: their active logs ended without a result marker and the
normal supervisor pause recorded exit `-15`. Bounded log paths were retained
in state:

```text
/var/log/skyrim-dev/combat-C04-attempt-4-20260922T103613Z.log
/var/log/skyrim-dev/authority-A04-attempt-2-20260921T224137Z.log
/var/log/skyrim-dev/population-L03-attempt-2-20260922T103613Z.log
/var/log/skyrim-dev/ui-U02-attempt-2-20260922T103613Z.log
```

No worker output was used to make a development decision during this repair.
No development lane was approved, retried, blocked, advanced, committed,
pushed, merged, reset, cleaned, checked out, restored, or discarded.

## Root cause

The reviewed V3.1 source capped admission inside `schedule()` with
`while len(self.processes) < limit`, but `run_once()` called `advance_lane()`
for every lane first. On resume, `refresh_control()` restored several paused
retry lanes to `RECOVERING`; the old `advance_lane()` branch then called
`start_worker(lane, recovery=True)` directly for each recovery lane. This
bypassed the scheduler cap, and `start_worker()` had no independent cap check.

## Exact code-level fix

1. `advance_lane()` no longer launches a `RECOVERING` worker.
2. `RECOVERING` remains in `RUNNABLE_STATES`, so the existing dependency,
   review, round-robin, control-plane, and rate-limit scheduler gates select it
   through `schedule()`.
3. `_worker_limit()` is shared by scheduling and direct admission.
4. `start_worker()` returns `False` before spawning when the tracked process
   count is already at the configured cap. Normal and recovery workers count
   identically.
5. The self-test accepts a dirty `PAUSED` lane with
   `CURRENT_PHASE_REVIEW` and no worker PID as a preserved recovery worktree;
   it does not alter lane state.

The configured cap remained `max_concurrent_workers=2`.

## Regression and validation matrix

The suite increased from 92 to 101 tests. The new regressions prove:

| Requirement | Proof |
| --- | --- |
| Resume with three recoveries | Real `run_once()` → `refresh_control()` → `advance_lane()` → `schedule()` path admits exactly two fake PIDs, never three. |
| No `advance_lane()` bypass | Direct calls for several `RECOVERING` lanes make zero admission calls. |
| Defense in depth | With two tracked workers, direct `start_worker()` returns `False`; patched `Popen` is not called. |
| Slot refill | Removing one of two workers lets the next scheduler pass start exactly one replacement and return to two. |
| Review gating | `NEEDS_SOL_REVIEW` consumes no slot while READY/RECOVERING lanes fill both slots. |
| CI/non-worker states | `WAITING_FOR_CI`, `LOCAL_VALIDATION`, `COMMITTING`, `PUSHING`, and review states create no worker process. |
| Rate limit | Existing one-probe semantics remain; a full two-worker cap prevents the probe from starting. |
| Spawn failure | A failed spawn does not consume a slot; the same bounded scheduler pass starts other eligible lanes up to two. |
| Paused review self-test | Dirty paused current-review worktrees pass preservation checks without lane mutation. |

Both local and installed VDS suites passed:

```text
101 tests, OK
```

The production commands all passed after the final daemon restart:

```text
skyrim-dev self-test          SELF_TEST_OK
skyrim-dev healthcheck        PASS
skyrim-dev worker-smoke-test  WORKER_SMOKE_OK
skyrim-dev status             GLOBAL: PAUSED; control plane VALID
skyrim-dev roadmap-status     valid: yes
skyrim-dev milestone-status   M01: ACTIVE
skyrim-dev review-status      GLOBAL: PAUSED; C04/A04/L03/U02 unchanged
```

The worker smoke test used only its disposable scratch workspace and proved:

```text
development worktree modified: NO
development branch changed: NO
persistent worker process: NO
```

Final production state:

```text
GLOBAL: PAUSED
orchestrator.service: active/enabled
skyrim-dev-healthcheck.timer: active/enabled
Codex development workers: none
control plane: VALID
```

Autonomous development was not resumed. The system is waiting for architect
review.
