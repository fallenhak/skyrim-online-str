# Character UI state machine and native bridge

This is the U02 design for the character-select UI. It describes the existing
client/session protocol and the smallest CEF-to-Angular surface needed to show
the server-owned character list and selection result. It does not add a
protocol, a create-character path, or any authority decision.

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
| `kAwaitingClientReady` | Successful native snapshot apply, or the current apply-failure path. On success, native immediately sends `CharacterReadyRequest`. | On the success path, only the native ready request is sent. Angular must not send it. | Waiting for server confirmation; an apply failure must be represented separately so the UI cannot mistake this state for success. |
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

## Angular projection states

The Angular state service should expose the following projection. The list,
empty, and selection-error/pending states are UI projections over the native
`kAwaitingCharacterSelection` value; they do not become a second authority.
`disconnected` and `connecting` remain transport states until native reports
that authenticated session state.

| Angular state | Entry signal | Exit signal and behavior |
| --- | --- | --- |
| `disconnected` | Existing `disconnect` callback or connection error. | A new successful `connect` callback starts a new list request. |
| `connecting` | Existing `connect()` command while transport is in progress. | Existing `connect` callback, error, or disconnect. No character data is retained from an older connection. |
| `characterListLoading` | Successful `connect`, followed by `requestCharacterList()`. | `characterList` with any array, including `[]`; keep loading if no response has arrived. |
| `characterListReady` | `characterList` with one or more server summaries. | A selection click enters `selectionPending`; disconnect clears it. |
| `characterListEmpty` | `characterList` with `[]`. | Remains empty until a new server list arrives or disconnect occurs. There is no local create action in this milestone. |
| `selectionPending` | `selectCharacter(characterId)` command. Store the requested ID only to correlate the view and prevent duplicate clicks. | `characterSelectionResult` returns a status. Never treat the stored ID as selected authority. |
| `selectionRejected` | A non-success `characterSelectionResult`. | Show the mapped status and re-enable selection only when native state is known to be `kAwaitingCharacterSelection`; otherwise wait for the state signal or reconnect. |
| `characterLoading` | `characterSelectionResult(kSuccess)` and the following native snapshot/apply signal. | Snapshot applied moves to ready-pending; apply failure is a blocking error/reconnect path until a reset exists. |
| `clientReadyPending` | Native snapshot apply succeeded and native sent `CharacterReadyRequest`. | `kProceed` moves to assignment-pending; mismatch returns to character selection; invalid state is an error with no optimistic transition. |
| `playerAssignmentPending` | `NotifyCharacterReadyResult(kProceed)` / native assignment-start signal. | Matching `NotifyCharacterEnteredWorld` moves to `inWorld`; disconnect resets. |
| `inWorld` | Matching `NotifyCharacterEnteredWorld`. | Disconnect resets the session and clears all character-selection data. |

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

## Bridge contract for U03 and later UI phases

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

Add these typed callbacks and commands using the existing camelCase naming
convention. These names are the U02 handoff names; they do not change the
network opcodes.

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

### State callbacks needed for the full flow

The minimum U03 list/result bridge is not enough to safely close the UI at world
entry. The later transition work needs a native projection of the already
existing dispatcher events, preferably one typed callback:

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

`OverlayService` can derive this callback from the existing connected,
disconnected, character selection, load-snapshot, snapshot-applied/failed,
ready-result, assignment-started, and entered-world events while reading
`CharacterSessionService::GetState()`. It must emit the state after the native
service has processed the event. A separate typed apply-failure callback is
also needed to distinguish the current apply-failure path from successful
`kAwaitingClientReady`; it should expose only a safe error code/message, not the
snapshot as an Angular-owned payload.

Angular must not receive or send `CharacterReadyRequest`, player assignment,
`AccountId`, owner profile, persistence internals, or native snapshot fields.
Native remains responsible for applying the snapshot, sending ready, assigning
the local player, and deciding when `InWorld` begins.

## Implementation handoff

U03 should update only the typed client/UI bridge and its browser/E2E adapter:

- add the callback/command types to `Code/skyrim_ui/src/typings.d.ts`;
- add native dispatcher connections and `ExecuteAsync` payload adaptation to
  `OverlayService`/`OverlayClient`;
- expose Angular RxJS subjects/commands through `NgZone` under
  `ClientService`; and
- keep the mock behind the same surface, using explicit server-response
  fixtures only. There must be no `createCharacter` or local character store.

U04-U08 can then implement the projection and transition behavior without
changing the server protocol. `PartyService` and its backend remain unrelated
and must not be removed or repurposed by this flow.

## Open safety questions for later phases

- The server has no explicit error response for a character-list request that
  is not currently allowed; the UI can only remain loading until a list or
  disconnect unless a bounded client timeout/error policy is added.
- On native snapshot-apply failure, the current client state is
  `kAwaitingClientReady` but native deliberately does not send ready. The UI
  must show a blocking failure/reconnect state; a retry/reset protocol is not
  implied by this document.
- The current `CharacterSessionService` has no public state-changed event. The
  state callback above must be a read-only projection of native state, not a
  setter exposed to Angular.
