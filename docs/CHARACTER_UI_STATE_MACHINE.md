# Character selection UX and session state machine

This document records the Character Select UX and its native session boundary.
The initial U02 design is now implemented through the UI edge-state work. It
describes the existing client/session protocol and the CEF-to-Angular surface
used to show the server-owned character list and selection result. It does not
add a protocol, a create-character path, or any authority decision.

The authoritative implementation points are:

- `Code/client/Services/CharacterSessionService.h/.cpp` for the client state
  machine and request facade;
- `Code/encoding/Structs/CharacterSummary.h` and
  `Code/encoding/Structs/CharacterSelectionStatus.h` for list data and result
  values;
- `Code/client/Services/Generic/OverlayService.cpp` and
  `Code/client/Services/Generic/OverlayClient.cpp` for the two CEF bridge
  directions; and
- `Code/skyrim_ui/src/typings.d.ts` and
  `Code/skyrim_ui/src/app/services/client.service.ts` for the Angular-facing
  callback and command surface.

## Authority and layers

There are three related but different states. They must not be collapsed into
one `connected` boolean.

1. The transport is connected after the server accepts authentication and
   `TransportService` dispatches `ConnectedEvent`.
2. The native character session is the source of truth for whether a list,
   selection, snapshot apply, ready response, assignment, or world sync is
   allowed.
3. Angular projects those native signals into loading, list, selection, and
   transition views. Angular never creates, edits, deletes, or selects a
   character locally; a click is only a request containing an untrusted ID.

`ConnectedEvent` is therefore not world entry. `NotifyCharacterEnteredWorld`
with the expected selected character ID is the only signal that permits the
client to enter `InWorld` and close the character surface. The snapshot is
consumed by native `CharacterApplyService`; its position, vitals, and other
fields are not an Angular data source.

## Canonical native state machine

`ClientCharacterSessionState` currently has these exact values:

| Native state | How it is entered | Allowed protocol work | UI meaning |
| --- | --- | --- | --- |
| `kDisconnected` | Initial state or `DisconnectedEvent`; pending snapshot is cleared. | No character protocol messages. | No character surface; clear all connection-scoped UI data. |
| `kAwaitingCharacterSelection` | Authenticated `ConnectedEvent`; also a character-ready mismatch reset. | `RequestCharacterList` and `SelectCharacterRequest`. | The server may provide a list and accept one selection. |
| `kCharacterSelected` | Successful `NotifyCharacterSelectionResult`. | No additional client action; the transport gate does not permit ordinary or character-list sends in this transient state. | Selection was accepted, but the UI must wait for native load progress. |
| `kApplyingCharacter` | `NotifyCharacterLoadSnapshot`; native caches the snapshot and `CharacterApplyService` applies it. | No UI request. | Loading/applying a server snapshot; do not show world-ready UI. |
| `kAwaitingClientReady` | Successful native snapshot apply, or the current apply-failure path. On success, native immediately sends `CharacterReadyRequest`. | On the success path, only the native ready request is sent. Angular must not send it. | Waiting for server confirmation. The current state value alone does not distinguish successful apply from apply failure. |
| `kAwaitingPlayerAssignment` | `NotifyCharacterReadyResult(kProceed)`; native dispatches `CharacterPlayerAssignmentStartedEvent`. | Native assigns only the local player reference. | Character accepted, but the server has not created/confirmed the persistent world player yet. |
| `kInWorld` | Matching `NotifyCharacterEnteredWorld`; native dispatches `CharacterWorldSyncStartedEvent`. | Normal gameplay protocol becomes available. | Close character select only here. |

The server has an additional identity-binding step before the client receives
`ConnectedEvent`: `kConnected -> kAwaitingIdentity ->
kAwaitingCharacterSelection`. `kIdentityNotReady` is consequently a server
session condition, not a client-owned identity input.

The successful native sequence is:

```text
Disconnected
  -> ConnectedEvent
  -> AwaitingCharacterSelection
  -> NotifyCharacterList
  -> SelectCharacterRequest
  -> CharacterSelectionResult(kSuccess)
  -> CharacterSelected
  -> NotifyCharacterLoadSnapshot
  -> ApplyingCharacter
  -> CharacterSnapshotAppliedEvent
  -> AwaitingClientReady + native CharacterReadyRequest
  -> NotifyCharacterReadyResult(kProceed)
  -> AwaitingPlayerAssignment + native local-player assignment
  -> NotifyCharacterEnteredWorld
  -> InWorld
```

The server sends the successful selection result and the load snapshot in that
order. A selection result by itself is not permission to close the UI or infer
that the local Skyrim player has been assigned.

## Character Select UX and Angular projection

The component renders four visual states: loading, list, empty, and error.
Selection-pending is shown on the selected row while the list remains visible;
after the server accepts selection, the list is replaced by a loading message.
These are UI projections over the native session, not a second authority.
`disconnected` and `connecting` remain transport states until native reports
the authenticated session state.

| Angular state | Entry signal | Exit signal and behavior |
| --- | --- | --- |
| `disconnected` | Existing `disconnect` callback or connection error. The visible screen shows a connection-lost error. | A new successful `connect` callback starts a new list request. |
| `connecting` / `characterListLoading` | Connect is in progress, or authenticated connection triggers `requestCharacterList()`. | `characterList` with any array, including `[]`; no previous connection's character data is retained. |
| `characterListReady` | `characterList` contains one or more server summaries. The UI displays the server-provided name, level, race form IDs, and sex. | A selection click marks that server ID pending and disables all selection buttons. |
| `characterListEmpty` | `characterList` is `[]`. The UI explains that no characters are available and that creation is unavailable from this screen. | Remains empty until another server list arrives or the connection is reset. No local create action exists. |
| `selectionPending` | `selectCharacter(characterId)` command. The pending ID is retained only for the row label and duplicate-click prevention. | A selection result arrives. The ID is never treated as selected authority. |
| `selectionRejected` | A non-success `characterSelectionResult` for the pending request. The UI shows a mapped error and permits backing out. | A later server list or new connection supplies the next selectable state. The UI does not remove or alter a row locally. |
| `characterLoading` | The server accepts selection or native reports selected, snapshot-applying, ready, or assignment-pending. The selection controls remain unavailable. | Matching `NotifyCharacterEnteredWorld` closes Character Select; disconnect resets it. |
| `inWorld` | Matching `NotifyCharacterEnteredWorld`. | Disconnect resets the session and clears all character-selection data. |

Character Select opens after the authenticated `connect` callback and requests
the list from the server. Escape/Back can close the screen while no selection
is pending or progressing. A successful selection cannot be dismissed early;
the surface closes only once native reports the matching character in-world.

On native snapshot-apply failure, the client currently also enters
`kAwaitingClientReady` but does not send `CharacterReadyRequest`. Since no
separate failure callback reaches Angular, the visible UI remains on the same
loading message used while awaiting server confirmation. This is an unresolved
failure-feedback/recovery gap; the state projection does not make the failed
apply look like world entry.

Required transition rules:

- `characterList` is a complete replacement, not an incremental local cache;
  an empty list is valid server data.
- `CharacterSelectionResult` does not contain a character ID. Keep a pending
  ID for UX only and wait for the native session transition/snapshot signals.
- `kNotFoundOrNotOwned` must remain a generic error. The UI must not disclose
  whether an ID exists for another owner and must not remove a row locally.
- A failed or duplicate click must not be treated as success. The UI must
  disable the row/selection action while the request is pending because a
  rejected transport send does not produce a server result.
- Disconnect from any state clears the list, pending ID, error, and state. A
  later connection is a new session, even if it uses the same server address.
- The legacy `playerConnected`, `playerDisconnected`, party, or local-player
  callbacks are not character-session transitions.

## Selection status values

The existing server enum is the complete result vocabulary and must remain
typed across the bridge:

| Numeric value | `CharacterSelectionStatus` | Angular behavior |
| ---: | --- | --- |
| `0` | `kSuccess` | Enter the native loading transition; do not close the UI. |
| `1` | `kIdentityNotReady` | Show that server identity is not ready; do not fabricate a list or identity. |
| `2` | `kNotFoundOrNotOwned` | Show a generic unavailable-character error; preserve server authority and allow a fresh list only if the native state permits it. |
| `3` | `kInvalidState` | Show a stale/out-of-sequence request error; do not infer selection or world entry. |

`CharacterReadyStatus` is not a selection result. It belongs to the native
ready/assignment transition and must not be reused as a character-list status.

## Implemented bridge contract

The existing bridge has this shape:

```text
native dispatcher event
  -> OverlayService::ExecuteAsync(name, CefListValue)
  -> browser skyrimtogether.on(name, callback)
  -> ClientService NgZone/RxJS subject
```

Commands travel in the opposite direction through the existing `ui-event`
message handled by `OverlayClient::OnProcessMessageReceived`.

### Minimum list/selection surface

These typed callbacks and commands use the existing camelCase naming
convention. They expose the existing protocol and do not change network
opcodes.

| Direction | Name | Payload/arguments | Native action |
| --- | --- | --- | --- |
| native -> Angular | `characterList` | One complete `CharacterSummaryBridge[]` argument. `[]` is meaningful. | Adapt `CharacterListReceivedEvent` from `NotifyCharacterList`. |
| Angular -> native | `requestCharacterList()` | No arguments. | Call `CharacterSessionService::RequestCharacterList()`. |
| native -> Angular | `characterSelectionResult` | `CharacterSelectionStatus` value (`0..3`) and connection generation. | Adapt `CharacterSelectionResultEvent`; do not add a client-selected ID. Ignore responses from an old generation or without a pending request. |
| Angular -> native | `selectCharacter(characterId: CharacterId)` | One canonical decimal character ID string. | Parse only for transport and call `CharacterSessionService::SelectCharacter`; the server validates ownership/state. |

The existing `connect` and `disconnect` callbacks remain transport lifecycle
signals. On `connect`, the character UI requests the list. No automatic local
selection is allowed.

### Precision-safe summary shape

`CharacterSummary` is currently {`CharacterId: uint64, Name: string,
Race: GameId, Sex: int32, Level: int32`}. JavaScript `number` is not a safe
representation for every `uint64`, and CEF `SetInt` is not a safe
representation for every unsigned 32-bit game ID. Use this logical Angular
shape:

```ts
type CharacterId = string; // canonical unsigned decimal; never Number()

interface CharacterGameId {
  baseId: string; // unsigned decimal, opaque to Angular
  modId: string;  // unsigned decimal, opaque to Angular
}

interface CharacterSummaryBridge {
  characterId: CharacterId;
  name: string;
  race: CharacterGameId;
  sex: number;
  level: number;
}
```

For the existing positional `CefListValue` convention, a safe wire encoding is
one nested row per summary, with these positions:

```text
characterList argument 0: list of rows
row[0]: CharacterId decimal string
row[1]: Name string
row[2]: Race.BaseId unsigned decimal string
row[3]: Race.ModId unsigned decimal string
row[4]: Sex int
row[5]: Level int
```

The Angular adapter converts rows to `CharacterSummaryBridge` objects before
publishing them. A JSON/string shortcut must not turn IDs into floating-point
values. Race IDs are opaque server-provided identifiers; Angular must not use a
local save or local character record to fill missing fields.

### State callback used for the full flow

The list/result bridge alone is not enough to safely close the UI at world
entry. The implemented bridge exposes a read-only projection of native session
state through one typed callback:

```ts
type CharacterSessionState =
  | 'disconnected'
  | 'awaitingCharacterSelection'
  | 'characterSelected'
  | 'applyingCharacter'
  | 'awaitingClientReady'
  | 'awaitingPlayerAssignment'
  | 'inWorld';

// native -> Angular
characterSessionState(state: CharacterSessionState): void;
```

`OverlayService` emits this callback from the native session-state change event
after `CharacterSessionService` processes the transition. There is not yet a
separate typed apply-failure callback. If one is added, it should expose only a
safe error code/message, never the snapshot as an Angular-owned payload.

Angular must not receive or send `CharacterReadyRequest`, player assignment,
`AccountId`, owner profile, persistence internals, or native snapshot fields.
Native remains responsible for applying the snapshot, sending ready, assigning
the local player, and deciding when `InWorld` begins.

## Create-character gap

Character creation is not available in the current session protocol or
Character Select UI. When the server returns an empty list, the player sees the
empty state and has no character to select, so this flow cannot enter the
world. There is no create button, `createCharacter` bridge command, or local
character store. Supporting creation requires a separate server-owned workflow
and protocol; the client must not turn a local Skyrim save into a character or
pretend creation succeeded. `PartyService` and its backend are unrelated to
this flow and remain intact.

## Open safety questions for later phases

- The server has no explicit error response for a character-list request that
  is not currently allowed; the UI can only remain loading until a list or
  disconnect unless a bounded client timeout/error policy is added.
- On native snapshot-apply failure, the current client state is
  `kAwaitingClientReady` but native deliberately does not send ready. The UI
  currently remains on its loading message because no distinct failure signal
  reaches Angular; a safe failure indication and recovery path remain future
  work. A retry/reset protocol is not implied by this document.
- The state callback is a read-only projection of native state, not a setter
  exposed to Angular.
