# Interaction Authority Parallel Lane

Branch: parallel/interaction-authority
Issue: #32
Base: 148021a9517bc77b53aa484f1b97b53192feff07

## Ownership boundary
Primary areas: InventoryService, MagicService interaction semantics, ObjectService, actor movement/factions/weapon stale-incarnation risks, persistent-player protections, corresponding DTOs/policies/tests and ACTOR_AUTHORITY_AUDIT.
Do NOT implement hit observation, contribution ledger, creature lifecycle generation, ESLoader changes, or UI.

## Queue
A01 Re-read ACTOR_AUTHORITY_AUDIT and verify remaining B/E findings against current source.
A02 Persistent-player isolation audit: prove NPC exceptions cannot mutate another PersistentCharacterComponent player; fix clear bypasses.
A03 Inventory non-owner NPC interaction: test owner/epoch/range/player exclusions, malformed item/update flags; fix high-confidence bypasses only.
A04 Equipment and weapon-drawn stale-incarnation audit; add epoch only if same-client reacquisition creates a real stale-packet bug.
A05 Movement update payload authority: stale epoch/reacquisition, finite values, entity membership, payload bounds. Fix only clear issues.
A06 Faction mutation authority: owner/incarnation, malformed entries, persistent-player isolation.
A07 AddTarget semantics research: caster-owned vs caster-less/environmental effects. Build pure authorization policy if repository evidence supports it.
A08 RemoveSpell/interrupt/spell target interaction follow-up without breaking environmental semantics.
A09 Object Activate/LockChange/AssignObjects trust and sender-range audit. Harden obvious spoof paths.
A10 ScriptAnimation/dialogue/subtitle presentation spoofing audit; add range/source checks where semantics are clear.
A11 Teleport/cell/reference movement malformed-input and authority review outside combat-owned code.
A12 Bound client-controlled collections/maps on touched interaction paths where practical and test rejection.
A13 Session/InWorld and stale-entity removal review for all touched handlers.
A14 Protocol roundtrip/epoch/malformed enum regression tests.
A15 Final independent lane security review.

## Hard boundaries
Do not redesign persistent inventory wholesale. Do not add XP/rewards. Do not create blanket owner checks for legitimate non-owner gameplay unless semantics prove it. Do not touch combat hit protocol or population loader.

---
# Population / ESLoader Parallel Lane

Branch: parallel/population-loader
Issue: #33
Base: 148021a9517bc77b53aa484f1b97b53192feff07

## Ownership boundary
Primary areas: Code/components/es_loader, server ModsComponent/form-id resolver/population policy/identity resolver and ActorPopulationTests/docs.
Do NOT modify combat, progression, inventory/magic/object interaction, or UI.

## Queue
L01 Independently review current opt-in actor record loader and identity resolver for real-modlist correctness.
L02 Fix loadorder.txt parsing hazards: CR/LF, whitespace, duplicate lines, missing files, deterministic ordering, filename normalization. Preserve metadata-only default behavior.
L03 Research TES4 plugin flags in loader. Correctly identify ESL/light namespace including ESL-flagged .esp if current extension heuristic is insufficient. Use plugin header authority, not client claim.
L04 Harden standard/light ID assignment and overflow/bounds; detect too many standard/light plugins cleanly.
L05 Verify master mapping/reference prefix resolution for NPC, RACE, ACHR across multiple masters and overrides.
L06 Verify override precedence when later plugins override master-defined ACHR/NPC/RACE records.
L07 Missing master / unresolved form behavior must remain explicit Unknown, never guessed.
L08 Harden chunk/record bounds for minimal NPC/RACE/ACHR parsing against truncated/malformed plugin data without crashing.
L09 Filename case/path normalization and mapping between network ModsComponent names and server load metadata; avoid CR/path mismatches.
L10 Data directory/loadorder absence and partial modlist diagnostics; no forced record loading.
L11 Add synthetic tests for standard plugins, true light plugins, ESL-flagged ESP, overrides, malformed records, duplicate/missing masters, unknown network IDs and spoofed prefix bits.
L12 Assess performance/memory of opt-in full record parsing; implement low-risk bounded/indexing improvements only if measurable from code.
L13 Expand conservative population policy configurability without speculating edge races. Preserve 10 normal humanoid defaults and Unknown for unsupported classes.
L14 Verify humanoid assignment gate works correctly with hardened loader metadata and no-record default.
L15 Cross-platform GCC/MSVC/path portability pass.
L16 Final independent parser/security review and focused tests.

## Hard boundaries
Record loading remains opt-in by default unless architecture evidence justifies a separate reviewed change. Never classify Unknown as Creature/Humanoid. No combat/reward/UI work.

---
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
