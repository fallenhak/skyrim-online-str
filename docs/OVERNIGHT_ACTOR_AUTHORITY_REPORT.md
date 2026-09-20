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

Pending.

### Remaining risks

- Health, projectile, and other concrete mutation/relay gaps remain until their
  focused implementation phases.

## Later phases

Sections for the message authority audit, session gate audit, health hardening,
creature authority, combat attribution research, contribution ledger, malformed
input pass, and final verification will be appended as those phases complete.
