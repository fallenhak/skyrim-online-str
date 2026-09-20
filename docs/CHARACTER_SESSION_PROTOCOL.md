# Character session and selection protocol

This milestone adds the protocol foundation for character selection without loading a character into Skyrim.

The server flow is:

```text
SessionService (connection)
    -> verified server-side identity binding
    -> RequestCharacterList / NotifyCharacterList
    -> SelectCharacterRequest
    -> owner-scoped validation / NotifyCharacterSelectionResult
```

`SessionService` is keyed by the live connection identity and stores an optional verified `OwnerProfileId`, the session state, and the selected persistence `CharacterId`. Client-supplied `DiscordId`, username, profile strings, and transient `PlayerId` values are not used as authenticated ownership.

Character list responses expose only network `CharacterSummary` fields: ID, name, race, sex, and level. Selection always calls the persistence repository's owner-scoped lookup. A missing character and a character owned by another profile produce the same `kNotFoundOrNotOwned` status.

The client service exposes request methods and dispatcher events for a future UI. Authentication, character creation/editing/deletion, UI/CEF, loading, applying, and spawning remain outside this milestone.
