# Character Selection / Co-op UI Parallel Lane

Branch: parallel/character-ui
Issue: #34
Base: 148021a9517bc77b53aa484f1b97b53192feff07

## Ownership boundary
Primary areas: Skyrim UI frontend, client UI bridge/services/events needed to expose the EXISTING character list/select/load state machine, UI tests/build/docs.
Server session/persistence protocol is read-only unless a tiny clearly missing UI-facing response field is required and proven safe.
Do NOT modify combat, actor authority, ESLoader or progression authority.

## Product target
launch/connect/authenticate -> character list/select -> selected snapshot/load -> ready -> InWorld.
Legacy party/co-op UI should no longer be the primary multiplayer entry surface once the replacement is functional.

## Queue
U01 Audit current skyrim_ui architecture, native UI bridge, party/player-list routing and existing character session client events/messages.
U02 Document exact UI state machine and identify native-to-Angular data bridge needed for CharacterSummary/list/selection status.
U03 Implement minimal typed client/UI bridge for character list notifications and selection result using existing protocol.
U04 Add Character Select route/screen with loading/empty/error/list states.
U05 Render server-provided character summaries only.
U06 Wire selection action to existing SelectCharacterRequest and prevent duplicate pending clicks.
U07 Handle selected/load-snapshot/ready/InWorld transitions so UI closes only at correct state.
U08 Reconnect/disconnect/error reset; stale lists must not survive a different connection.
U09 Keyboard/controller navigation and focus/back behavior.
U10 Remove/hide legacy party/co-op menu entry points that conflict with the new flow, without deleting PartyService backend.
U11 Audit old party/player-list auto-open assumptions after world entry.
U12 Add UI/state tests where supported; otherwise isolate pure state reducers/services.
U13 Build skyrim_ui/client integration and fix type/bridge issues.
U14 Edge states: zero characters, long names, invalid/failed selection, disconnect during selection.
U15 Update UX/session docs and explicitly note create-character gap.
U16 Final independent UI/state-machine review.

## Hard boundaries
No local fake character creation. No PartyService backend deletion. No authority/combat/progression changes. Preserve server as source of character list/selection truth.
