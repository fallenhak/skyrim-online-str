# Long-Haul Codex Engineering Plan

Repository: fallenhak/skyrim-online-str
Tracking issue: #30
Branch: hardening/longhaul-creature-combat
Base: 148021a9517bc77b53aa484f1b97b53192feff07

## Operating rule

Each fresh Codex invocation completes ONE substantial unfinished phase, updates STATE.md, creates focused commits, leaves a clean worktree, and exits. The external PowerShell supervisor pushes and starts the next fresh invocation.

Never merge, rebase, force-push, rewrite history, or modify the unrelated MemoryLayout.cpp C2127 problem.

Never make a client-provided persistent CharacterId, XP amount, reward amount, kill attribution, skill/level result, creature classification, or damage amount canonical.

## Priority queue

### P01 — Re-review overnight 21-commit batch
Independently review 34c651b8..148021a9. Check protocol serialization symmetry, opcode stability, ownership epoch propagation, stale-incarnation handling, finite-value validation, health sign conventions, and disconnect cleanup. Fix real regressions and add negative tests.

### P02 — Persist trusted population identity on server actor entities
Determine whether later combat/death code can reliably tell that a canonical entity was a trusted Creature. If not, add a small server-only component containing only trusted server-resolved population facts. Never serialize client claims into it.

### P03 — Server target lifecycle/incarnation identity
Implement a server-owned lifecycle generation/token that distinguishes canonical actor incarnations even if an entt entity ID is reused. Ensure removal clears it. Add deterministic reuse/cleanup tests.

### P04 — Validated hit-observation DTO
Define the smallest client-to-server observation DTO supported by actual repo state: attacker server ID + epoch, target server ID + current target incarnation evidence available to the client, bounded observation ID, and minimal diagnostics. No CharacterId, XP, reward, or authoritative damage. Append opcodes only. Add roundtrip tests.

### P05 — Attacker authorization policy
Validate InWorld sender, attacker existence, current ownership/epoch, attacker != target, and server-side mapping to an eligible persistent CharacterId when attribution requires it. Resolve CharacterId only on server. Keep unsupported summon attribution explicit.

### P06 — Target authorization policy
Validate target existence/current lifecycle, trusted Creature classification, non-player/non-humanoid exclusion, range/cell plausibility, and stale target rejection. PvP/self/unsupported targets must be explicit rejections.

### P07 — Bounded hit replay cache
Bind dedupe to attacker authority incarnation and target lifecycle generation. Add deterministic expiry/eviction, duplicate, zero-ID, rollover and entity-reuse tests.

### P08 — Client hit observation producer
Revisit Actor::HookDamageActor, HitEvent and disabled CombatService hit networking. Only implement if local forms can be mapped to the protocol's required current server identities safely. Do not send client-computed damage as authority.

### P09 — Server hit-observation handler
Wire DTO through gameplay/session gates and validation policies. Accepted observations become bounded internal pending observations only. Do not enter CombatContributionLedger and do not award XP yet.

### P10 — Health correlation foundation
Correlate pending validated hits with later accepted canonical target health decreases from the target's current simulation owner. A hit without a corresponding accepted health decrease must not become contribution. Treat healing separately. Use bounded time/tick windows.

### P11 — Death transition correlation
Only canonical alive-to-dead transition on current trusted Creature lifecycle becomes a death candidate. Death sender is not killer. Duplicate dead=true must not consume twice. Respawn/new lifecycle must not reuse old state.

### P12 — Integrate CombatContributionLedger
Only after P09-P11 are sound, record contributions from validated plus correlated observations. Consume once on canonical creature death into an INTERNAL result/event. Do not call ProgressionService and do not issue XP.

### P13 — Creature death contribution event
If useful, add a server-only event/result carrying target lifecycle and server-resolved persistent contributor CharacterIds. No reward amounts. No client message.

### P14 — Ownership transfer during combat
Stress attacker and target transfers between hit observation, health mutation and death. Old epochs must not inject observations. Document whether already-validated persistent contributions survive disconnect.

### P15 — Creature removal cleanup
Ensure pending observations, replay state, lifecycle metadata and contribution entries clear when target is removed without death and cannot leak into reused entity IDs.

### P16 — Movement/faction/weapon stale-epoch review
Return to category-B items in ACTOR_AUTHORITY_AUDIT.md. Add epochs only where same-client reacquisition can make stale packets relevant and client has correct epoch.

### P17 — Persistent player mutation isolation
Audit remaining actor mutation paths for ability to mutate another persistent player's actor through NPC interaction exceptions. Inventory, magic, package/respawn, dialogue and object paths deserve scrutiny. Add negative tests.

### P18 — AddTarget / RemoveSpell authority design
Research caster-less/environmental semantics. Implement only if server-known facts support a sound policy. Otherwise document exact unresolved cases instead of adding a false blanket owner rule.

### P19 — Inventory interaction boundary
Deep-review in-range non-owner NPC inventory exception. Protect persistent players absolutely. Implement high-confidence protections without breaking loot/pickpocket semantics.

### P20 — Interest-management and recipient audit
Audit SendToPlayersInRange and spawn/movement/combat relays for InWorld filtering, cell/world correctness, removed entities and stale actor lifecycle. Fix concrete leaks.

### P21 — Creature ownership handoff stress tests
Expand InvalidOwners, disconnect, decline chain, no-owner removal, mount/summon/player exclusion and epoch rollover tests. Fix real bugs.

### P22 — Malformed-input pass
Target combat/actor paths: invalid enums, NaN/Inf, zero/huge IDs, stale epochs, duplicate observation IDs, malformed vectors/maps, wraparound, missing entities and bounded-memory invariants.

### P23 — Progression boundary integration design
Define but DO NOT enable future mapping from canonical creature death contributions to server-chosen skill XP. Cover PvP/self exclusions, duplicate death and persistence gap. No reward producer.

### P24 — Skill/level persistence architecture spike
Using actual PlayerSkills structures, compare deterministic server-side Skyrim math vs award-ledger/replay baseline. Implement only neutral data structures/tests that do not trust client-computed results.

### P25 — Protocol compatibility/security regression suite
Cover append-only opcode invariants, epoch roundtrips, rejection messages, progression award DTO and hit DTO if implemented. Add malformed enum/field tests.

### P26 — Final independent security review
Review all long-haul commits as if written by another developer. Search trust regressions, stale entity reuse, unbounded containers, missing cleanup, incorrect sign math and crashable malformed inputs. Fix findings and rerun broad targeted tests.

## Continuous fallback pool

If all phases finish before the supervisor deadline, do not invent cosmetic work. Cycle through unresolved B/E risks in ACTOR_AUTHORITY_AUDIT.md, negative owner/epoch/session tests, stale entity reuse, bounded-memory checks, PartyService remnants, serialization symmetry, docs-vs-code mismatch, and GCC/MSVC portability in touched files.

Only commit concrete correctness, security, test, or architecture value.

## Hard boundaries

Do NOT implement production XP/rewards, client-selected CharacterId/XP/reward/damage authority, server-side Skyrim AI, production authentication, full persistent inventory redesign, wholesale Party/UI deletion, quest sync, static mass deletion of vanilla NPC placements, MemoryLayout.cpp workaround, or automatic merges.

## Verification

For implementation phases run narrow relevant tests and regularly:
- xmake -y SkyrimTogetherServer
- xmake -y SkyrimTogetherClient
- xmake -y TPTests
- xmake run TPTests
- xmake -y ActorPopulationTests
- xmake run ActorPopulationTests
- xmake -y SessionTests
- xmake run SessionTests
- xmake -y PersistenceTests
- xmake run PersistenceTests
- git diff --check

Aggregate xmake may remain blocked solely by known MemoryLayout.cpp C2127.
