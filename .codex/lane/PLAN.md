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
