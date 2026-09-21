# Combat Foundations Parallel Lane

Branch: parallel/combat-foundations
Issue: #31
Base: 148021a9517bc77b53aa484f1b97b53192feff07

## Ownership boundary
Primary areas: server combat/actor lifecycle components and services, combat-related encoding/messages, actor-value/death correlation required for combat, focused tests, COMBAT_AUTHORITY/CREATURE_AUTHORITY docs.
Avoid InventoryService/ObjectService/MagicService except read-only research. Avoid UI and ESLoader internals.

## Queue
C01 Independently review overnight combat/health/death/projectile hardening for concrete regressions.
C02 Persist server-trusted population identity on canonical actor entities so later death code can distinguish trusted Creature without reusing client claims.
C03 Add server-owned actor lifecycle/incarnation generation immune to entt ID reuse; cleanup on removal.
C04 Define append-only validated hit-observation DTO with no CharacterId, XP, reward or authoritative damage.
C05 Implement pure attacker authorization: InWorld, current owner/epoch, eligible persistent attacker identity resolved server-side.
C06 Implement pure target authorization: current lifecycle, trusted Creature, non-player/non-humanoid, range/cell plausibility.
C07 Add bounded replay/dedupe keyed by attacker authority incarnation + target lifecycle.
C08 Revisit client HitEvent producer. Implement only if current server IDs/epochs can be mapped safely.
C09 Add server observation handler; accepted observations become bounded pending observations only.
C10 Correlate pending observation with accepted canonical health DECREASE, not client damage magnitude.
C11 Correlate canonical alive->dead Creature transition; duplicate death does nothing; death sender is not killer.
C12 Integrate existing CombatContributionLedger only for validated+correlated observations. NO XP.
C13 Add internal server-only creature-death contribution event/result containing server-resolved CharacterIds only.
C14 Stress ownership transfer/disconnect during combat.
C15 Ensure removal/respawn/entity reuse clears lifecycle, replay, pending and contribution state.
C16 Malformed-input/bounded-memory/security regression pass.
C17 Final independent lane review and broad focused tests.

## Hard boundaries
No ProgressionService award call. No XP. No loot. No server-side Skyrim AI. No client-selected CharacterId/damage/kill. No PartyService/UI work.
