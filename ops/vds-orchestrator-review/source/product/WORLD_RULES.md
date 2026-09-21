# World Rules

These are architectural invariants for the project.

- Persistent player Characters are server-owned.
- AccountId and CharacterId are server authority.
- Local Skyrim save data is bootstrap/shell state, not persistent authority.
- Players replace the ordinary humanoid world population.
- Vanilla humanoid NPC population is suppressed by policy.
- Trusted creature/monster/undead population remains.
- Unknown actor classification is never guessed into Creature.
- Vanilla single-player quest state is not multiplayer authority.
- Clients may simulate Skyrim AI/runtime, but the server owns multiplayer identity, lifecycle, persistence, and trust.
- Never invent fake server-side Skyrim AI.
- Dungeons and encounters are renewable world population.
- A respawned creature receives a fresh lifecycle/incarnation.
- Stale packets from an earlier incarnation must not affect a respawned actor.
- Client-supplied XP, reward amount, damage, kill attribution, AccountId, CharacterId, population classification, or persistent ownership are never authoritative.
- PvP/self-damage must not accidentally become PvE progression evidence.
- No autonomous merge to main or between lane branches.
- Cross-lane integration requires an explicit reviewed integration step.
- Queue completion is not equivalent to product acceptance.
