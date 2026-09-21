# Milestone 01 — Core World

## Player-facing target

Launch Skyrim
→ connect/authenticate
→ Character Select
→ choose a server-owned character
→ apply character snapshot
→ enter world
→ ordinary humanoid NPC population is absent
→ multiple players coexist
→ trusted creatures remain and synchronize
→ enter a dungeon/encounter
→ creature combat/death/lifecycle remains coherent
→ cleared renewable population can reset safely
→ reconnect restores the same server character

## Engineering workstreams

- Character entry/UI
- Humanoid suppression and actor classification
- Creature lifecycle/combat authority
- Interaction/authority hardening
- Renewable dungeon/encounter runtime
- Persistence/reconnect validation

## Acceptance gate

Milestone 01 is not complete when queues are merely exhausted. Final acceptance requires reviewed multi-client runtime evidence on Windows with at least two Skyrim clients, including a reconnect and a renewable encounter reset scenario.

Any missing integration branch, client runtime test, protocol/trust change, persistence schema change, or cross-lane dependency is a human/Sol gate rather than something the scheduler may improvise around.
