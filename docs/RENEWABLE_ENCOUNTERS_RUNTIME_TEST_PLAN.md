# Renewable Encounters — Runtime Integration Test Plan (W10)

Status: plan. The world state model (W01–W09) is unit tested standalone; none of it is
wired to handlers yet. This document is the acceptance test for the integration: two
players clear a dungeon, it resets, they re-enter, and stale packets are rejected.

Scope: M01-WORLD renewable encounters only. Combat verification (C11), cell tracking
(Authority A11) and spawning belong to other lanes; this plan names the ports they must
call and what the server must log so the run can be judged from evidence, not memory.

## 1. What the integration must wire

| Port (`RenewableEncounterRegistry`) | Caller | When |
|---|---|---|
| `AddEncounter`, `AddEncounterCell`, `AddSlot` | server config loader | startup, before any player connects |
| `Restore(snapshot, nowTick)` | startup, after config | with `RenewableEncounterRepository::LoadAll()` |
| `SetPlayerCell(player, cell)` | A11 player cell record | every `EnterInterior/ExteriorCellRequest` the server accepts |
| `RemovePlayer(player)` + `ReleasePlayerClaims(player)` | session teardown | disconnect |
| `GetSpawnRequests(id)` → `ClaimSpawn(...)` → `CompleteSpawn(ticket, incarnation)` | spawner | cell load by the owning player |
| `ExpireSpawnClaims(nowTick, ttl)` | server tick | periodically |
| `RecordCanonicalCreatureDeath(registry, event, tick)` (`Services/RenewableEncounterDeathPort.h`) | dispatcher sink for Combat C11 `AcceptedCanonicalCreatureDeathEvent` | never from a raw client packet |
| `ReleaseIncarnation(incarnation)` | despawn / unload / ownership lost | actor leaves without dying |
| `GetIncarnationStatus(incarnation)` | every actor packet handler | drop the packet unless `Current` |
| `TryReset(id, nowTick)` / `GetResetBlocker` | server tick | periodically |
| `Snapshot(nowTick)` → `RenewableEncounterRepository::SaveAll` | persistence | on clear, on reset, on graceful shutdown |

### Required server log lines

Each line carries the encounter id as `cell/group` and the server tick.

- `[World] encounter cleared <id> tick=<t>`
- `[World] encounter reset <id> epoch=<old>-><new>`
- `[World] reset blocked <id> reason=<NotEligible|Occupied>` (rate limited)
- `[World] spawn claimed <id> slot=<slot> player=<p> ticket=<n>` / `spawn completed ... incarnation=<server>:<gen>`
- `[World] stale packet dropped incarnation=<server>:<gen> status=<Stale|Unknown> from=<player>`
- `[World] snapshot saved encounters=<n>` / `snapshot restored encounters=<n>`

## 2. Test environment

- Playable build from the M01 integration branch (triggered by the integration lane),
  server = the `SkyrimTogetherServer.exe` shipped with it.
- Skyrim SE `1.7.104.0` on both clients (matches `kSupportedGameVersions`).
- Two clients, players **A** and **B**, on separate machines or accounts.
- Test config: one small interior dungeon registered as one encounter with 2–3 slots
  spanning two cells (exercises `AddEncounterCell`), and a **reset cooldown of 60 s** so
  a run fits in minutes. A second, untouched encounter as control.
- Server log at info level, captured to a file for the run.

## 3. Scenarios

Each scenario lists steps, the expected result, and the evidence that proves it. A
scenario passes only if every expected log line appears and no forbidden one does.

### S1 — Two-player clear
1. A and B enter the dungeon. 2. Kill every creature (both players land hits).
- **Expected:** each creature spawns once (one `spawn completed` per slot, never two for
  the same slot even though both players loaded the cell). Every kill is recorded once;
  `encounter cleared` appears exactly once, after the last kill.
- **Forbidden:** a second `spawn claimed` for a slot while its ticket is open; `cleared`
  before the last creature died.

### S2 — Reset is blocked while occupied
1. After S1, wait out the cooldown with **B still inside** (either cell of the set).
- **Expected:** `reset blocked ... reason=Occupied`; creatures stay dead.
2. B moves to the second cell of the dungeon. **Expected:** still blocked (cell set).
3. B leaves the dungeon.
- **Expected:** `encounter reset ... epoch=0->1` on the next tick.

### S3 — Re-entry sees fresh creatures
1. A and B re-enter.
- **Expected:** new `spawn claimed/completed` lines with **epoch 1** and new
  incarnations (new lifecycle generation). Membership back to all alive.

### S4 — Stale packet rejection
1. Before S2's reset, capture (or replay with a test client) an actor update / hit /
   death packet for a creature of epoch 0.
2. After the reset, send it again.
- **Expected:** `stale packet dropped ... status=Stale`; no creature of epoch 1 changes
  health or dies; the encounter does not clear.
- Also send a packet naming an incarnation the server never issued:
  `status=Unknown`, same outcome.

### S5 — Disconnect during spawn
1. A enters alone so A owns the spawn; kill A's client (hard disconnect) right after the
   cell loads.
2. B enters.
- **Expected:** A's claims are released on disconnect; B claims the free slots and each
  creature spawns exactly once. If A reconnects and its client completes an old spawn,
  it is refused (no `spawn completed` for A's old ticket).

### S6 — Restart mid-cycle
1. Clear the dungeon (S1), note the remaining cooldown, stop the server gracefully.
- **Expected:** `snapshot saved`.
2. Start the server. **Expected:** `snapshot restored`; the dungeon is still cleared;
   creatures do not come back; reset happens after the **remaining** cooldown measured
   from the new start, not the full cooldown and not immediately.
3. Repeat with a live (not cleared) dungeon: after restart the epoch is one higher and
   anything sent from before the restart is dropped as stale/unknown.

### S7 — Abuse probes (expected limitations recorded, not failures)
- A client that lies about its cell can make an occupied dungeon look empty and trigger
  a reset (known M01 limitation, tracked for M05). Record whether it reproduces.
- A client sending a raw death packet must not clear anything: only C11-verified deaths
  count. **This one is a failure if it clears.**

## 4. Pass criteria

M01-WORLD is runtime-complete when S1–S6 pass on the playable build, S7's raw death probe
is rejected, and the server log for the run is attached to the PR. The W03 port exists; S1 and S4 need
it subscribed to the dispatcher on the integration branch.

## 5. Out of scope

Loot and container resets, quest-flagged actors, human NPCs (removed by design), and
cheat-resistant cell validation (M05).
