# Overnight Actor Authority Hardening Report

Repository: `fallenhak/skyrim-online-str`
Tracking issue: #28
Base milestone: `feature/humanoid-local-suppression`
Base commit: `34c651b88f8f35293bcb3dce225bf9bf56a666fe`

This report records the evidence, security decisions, implementation changes,
tests, commits, and remaining risks for the overnight hardening batch. Work is
kept on `hardening/overnight-actor-authority`; no history rewriting, merge, or
force-push is permitted.

## Phase 0 — Repository preparation

Inspected systems:

- `origin` and the target branch state.
- The working tree and ancestry of the expected base commit.

Findings:

- `git fetch origin` succeeded.
- `hardening/overnight-actor-authority` was created as a tracking branch and
  was already up to date after `git pull --ff-only`.
- The worktree was clean and `34c651b88f8f35293bcb3dce225bf9bf56a666fe`
  is an ancestor of `HEAD`.

## Phase A — Humanoid suppression re-review

### Inspected files and systems

- Server `CharacterService::OnAssignCharacterRequest` and `CreateCharacter`.
- Client `CharacterService` actor-added, actor-removed, rejection, processing,
  and disconnect paths.
- `PopulationSuppressionPolicy`, `PopulationDisableTracker`, and focused tests.
- `DiscoveryService::VisitForms`, `ProcessLists::highActorHandleArray`, and
  `GetNiNode` discovery filtering.
- `TESObjectREFR::DisableImpl` / `EnableImpl`, `TESForm::IsDisabled`, and
  deletion/temporary handling.
- Existing leveled-NPC disable/enable reconciliation.

### Findings and invariants

- Trusted placed identity is classified and rejected before `CreateCharacter`
  allocates a server ECS entity.
- Only the exact server reason `kPopulationHumanoidDenied` can request physical
  suppression. `kPopulationUnknownDenied` remains synchronization-only.
- The client consumes the assignment cookie, removes waiting state, and marks
  `PopulationSuppressedComponent` before attempting local suppression.
- Ownership is recorded by local reference form ID before the asynchronous
  `DisableImpl()` call. Player `0x14`, temporary, deleted, and already-disabled
  references are not claimed.
- Discovery only visits high-process actors with a `NiNode`; disappearance can
  therefore produce `ActorRemovedEvent` after a disable. ECS cleanup deliberately
  leaves the independent tracker entry intact.
- `ActorAddedEvent` recognizes tracked references, rebuilds suppression state,
  removes assignment-only state, and re-disables without another assignment.
- Disconnect swaps the tracker into a local restore set before clearing runtime
  suppression state, then calls `EnableImpl()` without requiring `IsDisabled()`.
- No population suppression path uses `Delete()` for placed humanoids.

### Changes implemented

- Added an explicit policy overload that makes cancelled assignments ineligible
  for physical suppression.
- Added regression coverage for cancelled suppression and an empty tracker after
  disconnect-style draining.

### Deliberately not implemented

- No independent client classification authority.
- No persistence of disabled form IDs.
- No creature authority, combat, XP, or inventory changes in this phase.

### Tests

- `xmake -y TPTests` — passed.
- `xmake run TPTests` — passed, 150 assertions in 18 test cases.

### Security implication

The server rejection remains the authority decision. Client disable state is
only presentation state for the current runtime connection and cannot grant a
server entity or ownership.

### Commit

- `70d8283bf15dae9956cfef680d62e171bb14a9c0` — guarded cancelled assignments
  from physical suppression and added the Phase A report.
- `72c4be1405af02044ff6315b2afe66995bee1f2c` — cleaned report formatting.

### Remaining risks

- Restoration is best effort if Skyrim cannot resolve a form during disconnect.
- A process crash can prevent runtime restoration.
- The server still has broader actor mutation surfaces under audit in Phase B.

## Phase B — Client-to-server actor authority audit

### Inspected files and systems

- `GameServer::BindMessageHandlers`, `SessionService`, and the client outbound
  session policy.
- Server packet handlers in `CharacterService`, `ActorValueService`,
  `InventoryService`, `CombatService`, `MagicService`, `ObjectService`,
  `PlayerService`, `OverlayService`, `WeatherService`, `MapService`,
  `CommandService`, `QuestService`, and `PartyService`.
- Client producers in `CharacterService`, `ActorValueService`,
  `InventoryService`, `CombatService`, `MagicService`, and `ObjectService`.
- All relevant request/notification DTOs and serialization paths.

### Findings

- The server's generated gameplay dispatch boundary requires `InWorld`; the
  explicit protocol handlers are the only pre-world paths.
- `OwnerView` filters by current owner pointer but not ownership epoch.
- Health lacks both current-owner validation and an epoch in the baseline.
- Projectile launch trusts the client-provided shooter ID.
- Package, spell, interrupt, target, remove-spell, script-animation, and some
  object paths are relay/mutation surfaces whose authority semantics need
  targeted treatment rather than a blanket owner check.
- Inventory deliberately supports non-owner in-range NPC interaction, so it
  requires a separate interaction policy and must distinguish persistent player
  actors from ordinary NPCs and objects.

### Changes implemented

- Added the full message/handler matrix and classifications to
  `docs/ACTOR_AUTHORITY_AUDIT.md`.
- Recorded the exact session gate boundary and high-confidence health,
  projectile, stale-epoch, package, magic, and object risks.

### Deliberately not implemented

- No mechanical epoch field was added to every message during the audit-only
  phase.
- No blanket owner requirement was imposed on legitimate non-owner interaction
  paths.
- No persistent inventory redesign, combat attribution, XP, or PartyService
  removal was attempted.

### Tests

- The audit-only changes reused the Phase A test run: `xmake -y TPTests` and
  `xmake run TPTests` passed with 150 assertions in 18 cases.

### Commit

- `81937855` — added the actor mutation authority audit and message matrix.
- `a4deafcc` — cleaned the audit document formatting.

### Remaining risks

- Health, projectile, and other concrete mutation/relay gaps remain until their
  focused implementation phases.

## Phase C — Health authority hardening

### Findings

- The client health event carries signed deltas: damage is negative, healing and
  regeneration are positive.
- The baseline server handler accepted any sender for any entity ID, used
  `operator[]` for the health entry, and applied the delta with the wrong sign.
- The baseline health messages had no ownership epoch, so delayed health updates
  could cross an ownership incarnation.

### Changes implemented

- Appended `OwnershipEpoch` to request and notification health messages without
  changing their opcodes or the existing `Id`/`DeltaHealth` field order.
- Required the server sender to be the current owner of the target entity and
  the current ownership epoch before applying or relaying a health delta.
- Applied signed finite deltas to the existing health entry without inserting a
  missing value, and rejected non-finite input and arithmetic overflow.
- Required the client producer to use a local actor with a non-zero ownership
  epoch, retained that epoch while coalescing small health changes, and cleared
  pending changes on disconnect.
- Applied remote health notifications only to a matching remote entity and
  ownership epoch, rejecting stale, non-finite, or unknown notifications.

### Deliberately not implemented

- No max-stat persistence or health clamping was introduced.
- No combat damage authority, projectile validation, XP, inventory, or session
  behavior was changed in this phase.

### Tests

- `xmake config --plat=windows --arch=x64 --mode=releasedbg --yes -vD` — passed.
- `xmake -y TPTests` — passed.
- `xmake run TPTests` — passed, 171 assertions in 20 test cases.
- `git diff --check` — passed before commit.

### Commit

- `ae6c8784` — required ownership epochs for health changes, enforced current
  owner validation, and added signed-delta regression tests.

### Remaining risks

- Projectile launch and other relay/mutation surfaces still need focused
  authority treatment.

## Phase D — Projectile launch authority hardening

### Findings

- The server previously copied the client-provided `ShooterID` directly into a
  broadcast and used it as the range origin without proving that the sender
  owned that actor.
- Projectile request and notification messages had no ownership epoch, so a
  delayed launch could be applied to a reused server entity ID.
- Origin, angle, power, and scale floats were accepted without finite-value
  validation before reaching the game launch path.
- Package, spell-cast, interrupt-cast, and respawn relay handlers also trusted
  actor IDs without an incarnation token; respawn additionally cleared an
  animation replay cache before checking whether the sender was the owner.

### Changes implemented

- Appended `OwnershipEpoch` to projectile request and notification messages,
  preserving the existing opcode and field order.
- Required a valid character entity, an owner component, the current sender
  owner, and a non-zero matching epoch before the server relays a launch.
- Rejected non-finite launch numeric parameters without imposing arbitrary
  gameplay caps.
- Required the client producer to send its local ownership epoch and required
  remote clients to match the notification to the current remote incarnation
  before invoking the engine projectile launch path.
- Added current-owner/epoch validation and matching notification epochs for
  package updates, spell casts, and cast interrupts.
- Added epoch validation to respawn requests and notifications, and moved the
  replay-cache mutation inside the current-owner branch. Remote observers must
  present the current epoch before requesting a fresh spawn snapshot.
- Added protocol round-trip coverage and pure authority/malformed-input tests.

### Deliberately not implemented

- No projectile damage attribution or hit validation was added here.
- Draw-weapon, factions, and movement already use current-owner filtered views;
  no redundant epoch field was added to those batch messages.
- AddTarget remains unresolved because the event supports caster-less effects
  and non-owner targets; it needs a separate interaction/range policy rather
  than a blind caster-owner requirement.
- No combat, XP, inventory, party, or session redesign was attempted.

### Tests

- `git diff --check` — passed.
- `xmake -y TPTests` — passed.
- `xmake run TPTests` — passed, 200 assertions in 22 test cases.
- `xmake -y SkyrimTogetherServer` — passed with existing compiler warnings.
- `xmake -y SkyrimTogetherClient` — passed with existing compiler warnings.

### Commit

- `20862de1` — enforced projectile shooter ownership/epoch and finite input
  validation.
- `f0118219` — added package, magic, and respawn owner/epoch validation and
  protocol regression coverage.

### Remaining risks

- Package, spell, object, and other actor mutation/relay surfaces remain under
  focused review.

## Phase E — Reusable authority test infrastructure

### Inspected systems

- The pure health and projectile authority policies and their focused Catch2
  tests.
- Server `OwnerComponent::IsCurrentOwner` and the existing ownership epoch
  transfer paths.

### Findings

- Both concrete policies need the same four facts: the entity exists, an owner
  exists, the sender is the current owner, and the epoch is non-zero.
- The domain-specific checks differ: health validates signed finite deltas and
  projectile launch validates finite launch parameters. They should not be
  merged into a broad mock or shared gameplay policy.

### Changes implemented

- Added the small reusable `ActorMutationAuthorityPolicy::IsCurrentOwner`
  predicate and made the health/projectile policies delegate to it.
- Kept tests pure and deterministic; they cover correct authority, missing
  entities/owners, wrong or stale authority, zero epochs, and malformed values.

### Deliberately not implemented

- No mock-world framework or test-only ownership model was introduced.
- No aesthetic refactor of existing handlers was performed.

### Tests

- `git diff --check` — passed.
- `xmake -y TPTests` — passed.
- `xmake run TPTests` — passed, 200 assertions in 22 test cases.

### Commit

- `8d79d648` — extracted the shared current-owner/epoch predicate used by the
  health and projectile policies.

### Remaining risks

- The pure policy tests do not replace integration coverage of every ECS
  handler; unresolved interaction semantics remain documented in Phase D/B.

## Phase F — Creature authority readiness audit

### Findings

- Local high-process actor discovery leads to `AssignCharacterRequest`; the
  server resolves identity/classification and creates the canonical ECS entity
  before publishing `CharacterSpawnedEvent` state to eligible remote clients.
- `OwnerComponent` and its non-zero epoch define the current simulation owner.
  The server validates owner/epoch-bound observations but does not simulate
  Skyrim AI.
- Owner loss queues either character removal for the player actor or an
  ownership search for other owned actors. The next owner must be connected,
  in range, and not in the current `InvalidOwners` set; successful transfer
  increments the epoch.
- Health and death are current-owner observations. They are not killer or
  reward attribution, and a stale epoch cannot mutate the canonical entity.
- `PartyService` is not consulted by creature creation, transfer, health,
  death, or disconnect authority. `AuthorityService::CanClaimActor` rejects
  social/party-role claims; the legacy party wording is diagnostic only.
- Client actor authority is server-coordinated. Client world authority comes
  from presence and is not an actor claim shortcut.

### Changes

- Added [`docs/CREATURE_AUTHORITY.md`](CREATURE_AUTHORITY.md) with the full
  discovery → assignment → spawn → ownership → health/death → transfer/remove
  lifecycle and the PartyService boundary.
- No production decoupling patch was necessary because the reviewed authority
  paths already use `CharacterService`, `OwnerComponent`, and
  `AuthorityService`, not party membership.

### Tests

- Documentation-only phase; no new production behavior or test target was
  introduced.
- The existing actor-population and authority-policy tests remain the relevant
  executable coverage and are rerun in the phase verification.

### Commit

- `b323c107` — documented the creature lifecycle, ownership transfer, and
  deliberate PartyService boundary.

## Phase G — Combat authority readiness audit

### Findings

- Skyrim exposes local hit observations with attacker and target form IDs, but
  the active network path does not contain a server hit-claim producer or
  handler; the existing `CombatService` hit/target code is disabled.
- Projectile launch, health delta, and death-state paths are observation
  relays. They now require current owner/epoch evidence, but they do not prove
  causation or identify a killer.
- A client-provided attacker ID, damage amount, XP amount, or persistent
  character ID is insufficient evidence for attribution. Ownership transfer
  and server entity reuse require an incarnation/epoch check.

### Changes

- Added [`docs/COMBAT_AUTHORITY.md`](COMBAT_AUTHORITY.md), defining current
  authority boundaries and a future bounded observation shape for attacker,
  target, epoch, lifecycle, and replay identity.
- Deliberately did not enable hit networking or implement damage, XP, loot,
  inventory, party, session, or reward behavior.

### Tests

- Documentation-only phase; no production behavior or test target was added.

### Commit

- `b323c107` — documented combat observation limits and the future validated
  attribution shape without enabling hit claims.

### Remaining risks

- Combat attribution remains unimplemented and must not be inferred from the
  current health/death relay.
- `AddTarget` still needs a separate caster-less/non-owner interaction policy.

## Later phases

## Phase H — Bounded combat contribution ledger

### Design

- Added a server-internal, header-only `CombatContributionLedger` with no
  packet or connection API. Callers must resolve the sender's current owner,
  epoch, and persistent character before recording an observation.
- Keys contain the target server entity ID and lifecycle generation, avoiding
  reuse of an old target record after ECS entity recycling.
- Contributors are persistent `CharacterId` values stored in ordered maps, so
  consumption order is deterministic. Repeated validated observations update a
  bounded count and last-observed tick rather than allocating another entry.
- Invalid zero/negative identities, zero target IDs/generations, new targets
  beyond the target bound, and new contributors beyond the per-target bound
  are rejected. Existing contributor counts saturate at `uint32_t` maximum.
- Entries expire by monotonic observation tick, can be cleared explicitly, and
  are consumed-and-erased once for a target death. Removing a character scans
  only the bounded ledger and does not require connection state.

### Deliberately not implemented

- No network hit producer, damage attribution, XP, loot, or reward consumer was
  added.
- No server handler currently calls the ledger; this is reusable infrastructure
  for a later validated observation path.

### Tests

- `git diff --check` — passed.
- `xmake -y TPTests` — passed.
- `xmake run TPTests` — passed, 241 assertions in 27 test cases.

### Commit

- `5c705369` — added the bounded deterministic contribution ledger and pure
  regression tests.

### Remaining risks

- The ledger cannot make a client observation truthful by itself; the future
  caller must enforce owner/epoch, target classification, range, replay, and
  health/death correlation first.

The malformed-input pass, final verification, and draft pull request will be
appended as those phases complete.
