# Character session and selection protocol

This milestone adds the protocol foundation for character selection without loading a character into Skyrim.

The server flow is:

```text
SessionService (connection)
    -> verified server-side identity binding
    -> RequestCharacterList / NotifyCharacterList
    -> SelectCharacterRequest
    -> owner-scoped validation / NotifyCharacterSelectionResult
    -> server-authoritative CharacterLoadSnapshot
    -> AwaitingClientReady
```

`SessionService` is keyed by the live connection identity and stores an optional verified `OwnerProfileId`, the session state, and the selected persistence `CharacterId`. Client-supplied `DiscordId`, username, profile strings, and transient `PlayerId` values are not used as authenticated ownership.

Character list responses expose only network `CharacterSummary` fields: ID, name, race, sex, and level. Selection always calls the persistence repository's owner-scoped lookup. A missing character and a character owned by another profile produce the same `kNotFoundOrNotOwned` status.

Normal gameplay packets are accepted by the server only for a future `InWorld` session. Pre-world gameplay packets are silently dropped, and the client does not begin its normal actor-assignment scan while `CharacterSessionService::IsGameplayActive()` is false.

The bootstrap Skyrim save is not persistent character truth. The snapshot is populated only from the owner-validated database record selected by the server session; authentication fields such as username, level, worldspace, cell, time, and Discord ID do not populate it.

The client caches the snapshot and dispatches `CharacterLoadSnapshotReceivedEvent`, but does not apply it to Skyrim, teleport, mutate actor values, spawn the player, or enter the world. Snapshot application and the final readiness/world-entry transition are the next milestone.

The client service exposes request methods and dispatcher events for a future UI. Authentication, character creation/editing/deletion, UI/CEF, loading, applying, and spawning remain outside this milestone.

## Local development identity binding

For local development only, the server can explicitly bind a live player session to a persistence owner profile. The feature is disabled by default and must be enabled with `Identity:bEnableDevelopmentIdentityBinding true` (or the equivalent setting in the server configuration). Enabling it logs a warning.

The dedicated server console requires the leading `/`; with the setting enabled, use `/DevBindIdentity <PlayerId> <OwnerProfileId>`. The registered command name is `DevBindIdentity`. It resolves the active `PlayerId` to its live connection and calls the existing server-internal `SessionService::BindIdentity` transition. It only accepts sessions awaiting identity, requires a non-empty manually supplied profile ID, and refuses to overwrite an existing binding.

This command is not a client network message and is not authentication. Production startup and session handling must not rely on it; a real verified identity provider is expected to replace this development bridge later. Player ID and username are used only to locate and report the live session, never as the stored owner profile.
