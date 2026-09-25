# L2 bot: two scripted clients against a real server in CI

Status: design, not implemented. Owner: Claude (taken over from batudev0 on 2026-09-26, per Burak).
Context: #40 comment 5835523149 (item 3), 6th test analysis (5837987089).

## Goal
Catch plumbing bugs (dropped packets, 8-bit counters, missing epochs, silent rejects)
before a human test. A human test should only answer "how does it feel in game".

## Shape
- New target `Code/tests/l2bot` (console exe, no game). It links `encoding`, `common`
  and TiltedConnect's `TiltedPhoques::Client`, the transport the game client uses.
- CI job `l2bot` (Linux amd64): build `server_runner` and `l2bot`, start the server on
  localhost with a temp data dir, run the scenario, fail on any assertion or on a
  `[Drop]` / `[Desync]` line in the server log that the scenario did not expect.
- Auth: the server verifies an HMAC session token (`GameServer.cpp`, AuthTokenVerifier).
  CI sets a throwaway secret and the bot signs its own tokens with it
  (`Auth::` helpers are already shared with the tests).

## Game data without Skyrim.esm
CI must not carry Bethesda files. The server already runs without plugins, and plugin
features (containers, zones, leveled lists) come from `ESLoader`. Plan: a tiny fixture
plugin `Code/tests/l2bot/fixture/L2Fixture.esm`, built in the test from bytes (TES4 header,
one CELL, one CONT with an LVLI, one ECZN, one LVLN + NPC_ + ACHR, one DOOR, one ACTI).
Our own parser reads it, so the fixture also tests the parser.

## Scenario v1 (both bots in the same cell)
1. Connect, authenticate, select or create a character, enter the fixture cell.
2. `AssignObjects` with 300 objects: both get the same server ids (varint counters).
3. Bot A opens the door, B receives `NotifyActivate`; late join: C connects, gets the state.
4. Bot A takes an item from the container; B's view of the contents shrinks; restart the
   server and check persistence (schema v7).
5. The leveled actor: both bots see the same server pick (`LeveledNpcPickId`).
6. Bot A dies and respawns: B sees alive, full vitals, a new epoch (#83).
7. Bot A fires an arrow (bow `CastingSource`): accepted, relayed.
8. Lever: activation relayed; a late joiner gets the final binary state.

## Deliberately out of scope
Animation, physics, AI, UI: those need the game. The bot checks messages and state only.

## Steps
1. Fixture plugin writer plus parser round-trip test (no network).
2. `l2bot` connect + auth + cell entry against `server_runner`.
3. Scenario steps 2–8, one PR each, each with a CI assertion.
