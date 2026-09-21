# World Rules

These are immutable architectural truths for the persistent multiplayer world:

- A persistent player Character is server-owned.
- AccountId and CharacterId are server authority.
- Local Skyrim save data is bootstrap/shell state, not persistent authority.
- Players replace the ordinary humanoid world population.
- Vanilla humanoid NPC population is suppressed by policy.
- Creature/monster/undead population remains according to trusted server classification.
- Unknown actor classification must not be guessed into Creature.
- Vanilla single-player quest state is not multiplayer authority.
- Clients simulate Skyrim runtime/AI where required, but the server owns multiplayer identity/lifecycle/persistent decisions.
- Never invent fake server-side Skyrim AI.
- Dungeons/encounters are renewable world population.
- Dungeon/encounter population must eventually reset under server-controlled rules.
- A respawned creature must be a fresh lifecycle/incarnation so stale packets from a previous incarnation cannot affect it.
- PvP/self-damage must not accidentally become PvE progression evidence.
- Security/trust correctness beats feature velocity.
