# Character UI architecture audit

Issue #34, UI lane phase U01. This audit records the existing client/UI boundaries
before the character-selection surface is added. It does not add a new protocol,
create-character path, or change server/session authority.

## Existing client-side session path

`World` constructs `CharacterSessionService`, `CharacterApplyService`, and
`OverlayService` against the same `entt::dispatcher`. `TransportService` uses the
server-message factory to dispatch decoded messages onto that dispatcher. The
character session service already owns the protocol-facing transitions:

| Server/client signal | Existing client handling | UI visibility today |
| --- | --- | --- |
| `ConnectedEvent` | Resets the pending snapshot and enters `kAwaitingCharacterSelection`. | `OverlayService` emits the legacy `connect` callback; Angular marks the transport connected. |
| `NotifyCharacterList` | Emits `CharacterListReceivedEvent` containing `CharacterSummary` values. | No CEF or Angular listener. |
| `NotifyCharacterSelectionResult` | Emits `CharacterSelectionResultEvent`; success enters `kCharacterSelected`. | No CEF or Angular listener. |
| `NotifyCharacterLoadSnapshot` | Caches the snapshot, enters `kApplyingCharacter`, and emits `CharacterLoadSnapshotReceivedEvent`. | `CharacterApplyService` consumes it; no UI listener. |
| `CharacterSnapshotAppliedEvent` / failure | Sends `CharacterReadyRequest` on success; retains a pre-world state on failure. | No CEF or Angular listener. |
| `NotifyCharacterReadyResult` | Enters `kAwaitingPlayerAssignment` on `kProceed`; returns to selection on a mismatch. | No CEF or Angular listener. |
| `NotifyCharacterEnteredWorld` | Requires the expected selected character, enters `kInWorld`, and emits `CharacterWorldSyncStartedEvent`. | No CEF or Angular listener. |

`CharacterService` starts the normal actor/world synchronization only after
`CharacterWorldSyncStartedEvent`. `TransportService::CanSendMessage` also gates
outbound packets by this session state, allowing only the existing character
protocol messages before world entry and only the local player reference during
the assignment window. The UI must therefore request selection through the
existing service; it must not synthesize a player assignment or infer readiness
from a local Skyrim form.

The server exposes only the network-safe `CharacterSummary` fields to the list:
character ID, name, race, sex, and level. `CharacterLoadSnapshot` is a separate,
server-authoritative payload used by the native apply service. The UI should render
the summaries it receives and must not add local character records or creation
metadata.

## Native-to-Angular bridge

The native UI is a CEF overlay:

1. `OverlayService` owns the `OverlayApp` and calls `ExecuteAsync(name, args)` for
   native-to-browser callbacks.
2. `OverlayClient::OnProcessMessageReceived` receives the browser's `ui-event`
   messages, extracts an event name plus a `CefListValue`, and routes the currently
   supported commands to transport/chat/party services.
3. `src/typings.d.ts` declares the global `skyrimtogether` callback and command
   surface; `ClientService` subscribes to those callbacks and invokes commands.
4. Browser-only and E2E builds replace that global object with the existing mock in
   `src/app/mock`.

The bridge currently covers connection lifecycle, chat, player presence, health,
party, debug, and error events. It has no character-list, selection-result,
snapshot/apply, ready, assignment, or entered-world event names, and no
`requestCharacterList` or `selectCharacter` command. `OverlayService` likewise has
no dispatcher connections for the character events. The future bridge must keep
the server's values and statuses intact across this process boundary and must not
turn the legacy `connect`/`disconnect` callbacks into character-session signals.

## Legacy multiplayer routing

The current Angular route is transport-oriented rather than character-oriented:

- `RootComponent` shows the UI under `inGame$` and opens the Connect view before
  a connection. `ConnectComponent` closes that view after the native `connect`
  callback drives `connectionStateChange` to `true`.
- Once connected, the root menu exposes `PLAYER_MANAGER` plus the legacy player
  list and party menu. `UiRepository` defaults the manager to `PARTY_MENU`.
- `GroupComponent` renders party members whenever the overlay is active in-game;
  `PlayerListService` builds its list from `playerConnected`/
  `playerDisconnected` presence callbacks and resets it on transport disconnect.
- None of these services consumes `CharacterListReceivedEvent` or knows the
  character session state.

This means the character surface needs an explicit bridge and a pre-world UI entry
point. Removing or hiding PartyService behavior is not part of that work: the
PartyService backend and legacy in-world social UI remain separate concerns until
the replacement flow is functional.

## Handoff constraints

- Preserve `CharacterSessionService` and the existing server protocol as the
  source of truth.
- Keep character IDs and status values typed at the CEF/Angular boundary; do not
  accept a client-supplied character name, race, level, or snapshot as authority.
- Keep browser/E2E mocks behind the same typed surface so UI tests do not invent a
  second protocol.
- Do not expose character creation, editing, deletion, or persistence internals.
- Treat disconnect/reconnect as a new session: no character list or pending
  selection may be reused across connections.

The U02 state-machine and bridge payload contract is captured in
[`CHARACTER_UI_STATE_MACHINE.md`](CHARACTER_UI_STATE_MACHINE.md). U03 can then
implement only the typed native/browser bridge identified there.

## Validation

- Forty-nine focused positive source-symbol assertions and seven negative UI
  assertions confirm the client session transitions, native overlay
  callbacks/commands, Angular callback subscriptions, legacy routing, the absence
  of a character bridge, and the server-provided `CharacterSummary` fields
  described above.
- `git diff --check` passes.
- The Angular build is unavailable in this checkout because `pnpm` is not
  installed, `Code/skyrim_ui/node_modules` is absent, and the npm build script
  cannot resolve the local `ng` executable.
- `xmake show -t TPTests` was attempted, but configuration stopped before
  compilation because the environment has no discoverable C++ compiler
  (`cannot get program for cxx`).

## Recovery status

This document is the retained U01 work product from the interrupted iteration.
The source inventory and rerun assertions support completing U01; the U02
design is now recorded separately. Recovery could not complete lane bookkeeping:
`.codex/lane` is read-only in this worker mount, and the linked worktree
metadata rejects Git's `index.lock` creation (`git restore` and `git add` both
fail with permission denied). Earlier recovery notes referred to a tracked
`STATE.md` deletion; current Git status has no such deletion. It reports only
the retained U02 documentation paths, so no `STATE.md` restoration is required.
