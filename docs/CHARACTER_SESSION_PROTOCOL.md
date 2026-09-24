# Character session, character apply, and world-entry protocol

This milestone applies the owner-validated V1 character snapshot to the local Skyrim player and completes world entry only after the server has created the corresponding persistent player entity.

The server flow is:

```text
SessionService (connection)
    -> verified server-side identity binding
    -> RequestCharacterList / NotifyCharacterList
    -> SelectCharacterRequest
    -> owner-scoped validation / NotifyCharacterSelectionResult
    -> server-authoritative CharacterLoadSnapshot
    -> AwaitingClientReady
    -> CharacterReadyRequest
    -> AwaitingPlayerAssignment
    -> local PlayerCharacter (form 0x14) AssignCharacterRequest
    -> persisted player entity / NotifyCharacterEnteredWorld
    -> InWorld
```

The client follows the corresponding sequence `AwaitingClientReady -> ApplyingCharacter -> AwaitingClientReady -> AwaitingPlayerAssignment -> InWorld`. Applying the snapshot validates all basic values and resolves the race, worldspace, and cell before mutating Skyrim. It then applies the V1 fields: name, race, sex, level, location, and current health/magicka/stamina. Inventory, equipment, perks, skills, XP, spells, shouts, factions, and appearance are not authoritative in this milestone, apart from the safe race/sex refresh.

`SessionService` is keyed by the live connection identity and stores an optional verified `OwnerProfileId`, the session state, and the selected persistence `CharacterId`. Client-supplied `DiscordId`, username, profile strings, and transient `PlayerId` values are not used as authenticated ownership.

Character list responses expose only network `CharacterSummary` fields: ID, name, race, sex, and level. Selection always calls the persistence repository's owner-scoped lookup. A missing character and a character owned by another profile produce the same `kNotFoundOrNotOwned` status.

Normal gameplay packets are accepted by the server only for an `InWorld` session. A valid ready request advances only to `AwaitingPlayerAssignment`; it never enters `InWorld` directly. During that intermediate state, the only allowed assignment is the local player reference (`GameId(0, 0x14)`). The client transport gate also allows only the protocol messages and that one local assignment. Ordinary gameplay and the full actor-assignment scan remain disabled until `NotifyCharacterEnteredWorld` is received.

The bootstrap Skyrim save is not persistent character truth. The snapshot is populated only from the owner-validated database record selected by the server session. On final assignment the server reloads that record owner-scoped and its V1 name, level, worldspace, cell, position, and current health/magicka/stamina override client-provided bootstrap values. Authentication fields such as username, level, worldspace, cell, time, and Discord ID do not override the persisted record.

Once the persistent player is in-world, `CharacterSaveService` periodically saves only the live ECS location (`WorldSpace`, `Cell`, and position) and current Health/Magicka/Stamina. It also attempts the same save from `PlayerLeaveEvent` before the entity is removed. The save-back owner is the trusted `OwnerProfileId` captured during owner-scoped assignment; the SQL update checks both character ID and owner profile. It never writes Name, Race, Sex, or Level, and it does not write inventory/equipment, skills, perks, XP, appearance, or actor max/permanent values. `PlayerLevelRequest` remains a legacy runtime path only for nonpersistent players and never changes `CharacterRecord.Level`.

The client caches the snapshot, applies it through `CharacterApplyService`, dispatches `CharacterSnapshotAppliedEvent`, and sends only the snapshot `CharacterId` in `CharacterReadyRequest`. A validation or native-form resolution failure dispatches `CharacterSnapshotApplyFailedEvent` and keeps the client pre-world. After the server confirms readiness, the client assigns only the local player; after the server creates the persistent entity and transitions its session to `InWorld`, it sends `NotifyCharacterEnteredWorld`. The client then dispatches `CharacterWorldSyncStartedEvent` and performs the normal actor scan exactly once. While the connection is accepted, local skill-XP application and persistent-player `PlayerLevelRequest` updates are blocked; progression awards are a separate server-to-client path documented in `PROGRESSION_AUTHORITY.md`.

Authentication/transport connection is not global persistent-world presence. `PlayerJoinEvent` remains a legacy connection-level event, while `PlayerEnterWorldEvent` is the boundary at which `PresenceService` publishes the persisted character's name, level, and cell/worldspace through global presence messages. A client may cache remote presence messages received near world entry, but local world authority stays inactive until `CharacterWorldSyncStartedEvent` confirms the local character is in-world.

## Character-select UX and create-character gap

After authenticated connection, the client opens Character Select and requests a complete character list from the server. The UI renders only the returned `CharacterSummary` values and sends a selection request only for an ID from that list. A list response of `[]` is a valid empty state; it does not trigger local character creation or a fallback to the bootstrap save. Selection success begins the native snapshot and readiness sequence, and the UI remains open until matching `NotifyCharacterEnteredWorld` confirms world entry.

Character creation is not available from the current Character Select screen or session protocol. A player whose server list is empty therefore has no character to select and cannot enter the world through this flow. The UI states that creation is unavailable from this screen and offers no create action. A future creation feature needs a separate server-owned workflow and protocol; local saves, client-supplied character fields, or UI-only records cannot establish character identity or ownership.

The client service exposes the request methods and dispatcher events consumed by the current character-selection UI. Authentication-provider integration, character creation/editing/deletion, inventory persistence, XP producers, and later session features remain outside this milestone.

## Local development identity binding

For local development only, the server can explicitly bind a live player session to a persistence owner profile. The feature is disabled by default and must be enabled with `Identity:bEnableDevelopmentIdentityBinding true` (or the equivalent setting in the server configuration). Enabling it logs a warning.

The dedicated server console requires the leading `/`; with the setting enabled, use `/DevBindIdentity <PlayerId> <OwnerProfileId>`. The registered command name is `DevBindIdentity`. It resolves the active `PlayerId` to its live connection and calls the existing server-internal `SessionService::BindIdentity` transition. It only accepts sessions awaiting identity, requires a non-empty manually supplied profile ID, and refuses to overwrite an existing binding.

This command is not a client network message and is not authentication. Production startup and session handling must not rely on it; a real verified identity provider is expected to replace this development bridge later. Player ID and username are used only to locate and report the live session, never as the stored owner profile.
