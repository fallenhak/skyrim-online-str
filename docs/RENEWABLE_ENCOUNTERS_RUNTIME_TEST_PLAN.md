# Renewable Encounters — Runtime Integration Test Plan (W10, updated for W14)

Status: ready for the first M01 test build. `RenewableEncounterService` (W11–W14) is wired
to the server: Combat C11 deaths, the server's cell tracking, player disconnects, the tick,
spawned/removed actors and persistence. This plan is the acceptance test on a playable
build: two players clear Bleak Falls Barrow, it resets, they re-enter and find fresh
creatures, and the state survives a restart.

Every scenario is judged from the server log. The log lines below are the ones the code
actually prints; a scenario passes only if every expected line appears and no forbidden
one does.

## 1. Test environment

- Test build from `integration/m01-test`, installed through the launcher. Login is
  **Discord → Play**; the client connects automatically. There is no save loading and no
  Helgen intro: a new character is created in a slot and spawns in the **Temple of
  Kynareth, Whiterun**.
- Skyrim SE `1.7.104`, two clients: players **A** and **B**.
- Config `Data/renewable_encounters.txt` next to the server executable, shipped by the
  launcher (copy of `docs/renewable_encounters.example.txt`): Bleak Falls Barrow as one
  encounter over cells `000371DE` + `000371DD`, 38 slots, **cooldown=60 s**. Use the
  1800 s default in play.
- Server log at **info** level, captured to a file for the whole run.
- After the run: `python Tools/Scripts/encounter_log_report.py <server.log>` prints each
  encounter's clear/blocked/reset order and flags log-provable problems (missing config,
  double clear, reset without clear, failed saves). Exit code 1 means problems.
- Travel: walk (or `coc`) from Whiterun to Bleak Falls Barrow. The temple is not an
  encounter cell, so spawning there does not affect occupancy.

### Startup check (before anyone connects)

- **Expected:** `[World] renewable encounters loaded encounters=1 cells=1 slots=38 errors=0`
  `cells` counts only the extra `cell` lines; the owning cell (`000371DE`) is always
  covered, so the dungeon spans two cells while the log reports `cells=1`.
  followed by `[World] snapshot restored encounters=...`.
- **Failure:** `[World] no renewable encounter config at <path>, none configured` (warn) —
  the launcher put the file in the wrong place; every scenario below is void.

## 2. Log lines

| Line | Level | Meaning |
|---|---|---|
| `renewable encounters loaded encounters=<n> cells=<n> slots=<n> errors=<n>` | info | config read at startup |
| `no renewable encounter config at <path>` | warn | config missing |
| `snapshot restored encounters=<n>` / `snapshot saved encounters=<n>` | info | persistence |
| `snapshot skipped <n> encounter(s) no longer configured` | warn | stored state for a removed encounter |
| `spawn completed slot=<ref> incarnation=<server>:<gen> tick=<t>` | info | a placed actor bound to its slot |
| `spawn rejected slot=<ref> ...` | warn | a second live actor for an occupied slot |
| `encounter cleared <cell>/<group> tick=<t>` | info | last slot died (C11-verified) |
| `reset blocked <cell>/<group> reason=Occupied tick=<t>` | info | cooldown over, a player is inside; every 30 s |
| `encounter reset <cell>/<group> epoch=<old>-><new> tick=<t>` | info | new epoch |
| `stale actor removed incarnation=<server>:<gen> tick=<t>` | info | W14: an actor of the old epoch is removed on reset |
| `spawn claims expired` / `spawn claims released` | info | server-driven spawner only; not expected with vanilla placed actors |

## 3. Scenarios

### S1 — Two-player clear
1. A and B enter Bleak Falls Barrow. 2. Kill every hostile creature in both cells (both
   players land hits).
- **Expected:** one `spawn completed` per creature as it loads; `encounter cleared
  371de/0` exactly once, after the last kill; `snapshot saved`.
- **Forbidden:** `cleared` before the last creature died; two `spawn completed` for the
  same slot in the same epoch.

### S2 — Reset is blocked while occupied
1. After S1, wait out the 60 s cooldown with **B still inside** (either cell).
- **Expected:** `reset blocked 371de/0 reason=Occupied`; creatures stay dead.
2. B moves to the other cell. **Expected:** still blocked (the cell set counts).
3. B leaves the dungeon.
- **Expected:** `encounter reset 371de/0 epoch=0->1` on the next tick, then one
  `stale actor removed` per actor still loaded from epoch 0 (W14, PR #45 only), then
  `snapshot saved`.

### S3 — Re-entry sees fresh creatures
1. A and B re-enter.
- **Expected:** new `spawn completed` lines with new incarnations; creatures alive and
  hostile again. With PR #45 no corpse from epoch 0 remains. Without #45, old corpses may
  still be visible; that is expected on builds that do not include it.

### S4 — Corpses do not count as new creatures
1. After S1 and **before** the cooldown ends, A leaves and re-enters.
- **Expected:** corpses load, no `spawn completed` for them (dead actors are not bound),
  the encounter stays cleared, no reset.

### S5 — Disconnect inside the dungeon
1. A alone inside after S1; hard-kill A's client.
- **Expected:** A no longer blocks the reset: after the cooldown, `encounter reset` without
  A leaving through a door.

### S6 — Restart mid-cycle
1. Clear the dungeon (S1), note the tick, stop the server gracefully.
- **Expected:** `snapshot saved`.
2. Start the server. **Expected:** `snapshot restored`; the dungeon is still cleared;
   creatures do not come back; reset happens after the **remaining** cooldown from the new
   start, not the full cooldown and not immediately.

### S7 — Abuse probes (recorded, not all failures)
- A client that lies about its cell can make an occupied dungeon look empty and trigger a
  reset (known M01 limitation, tracked for M05). Record whether it reproduces.
- A raw client death packet must not clear anything: only C11-verified deaths count.
  **This one is a failure if it clears.**

## 4. Pass criteria

M01-WORLD is runtime-complete when the startup check and S1–S6 pass on the test build,
S7's raw death probe is rejected, and the server log for the run is attached to the PR.
The W14 lines in S2/S3 apply once PR #45 is in the build.

## 5. Out of scope

Loot and container resets, quest-flagged actors (left out of the config), human NPCs,
per-packet stale-incarnation gating (W14 removes stale actors instead; a packet gate can be
added later as an extra guard) and cheat-resistant cell validation (M05).
