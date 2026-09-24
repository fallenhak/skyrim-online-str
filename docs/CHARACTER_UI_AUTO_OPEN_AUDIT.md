# Character UI legacy auto-open audit

Issue #34, UI lane phase U11. This audit checks whether legacy party/player-list
visibility is tied to local game startup, transport connection, or server
character world entry.

## Findings

- `OverlayService::Render` reports `enterGame` when the local Skyrim player and
  3D node exist. Angular's `inGameStateChange` mirrors that local game condition;
  it is not the server character session's world-entry signal.
- `ConnectedEvent` marks an authenticated transport connection. `RootComponent`
  intentionally opens Character Select at that point. It does not open the old
  player manager, and a successful connection is not world entry.
- The old player-manager popup has no current `View` enum value, root-menu entry,
  or root-template switch case. `UiRepository` still retains a default
  `PARTY_MENU` tab value, but nothing in the current root can open that popup.
  `PlayerListService` updates its data and does not open a view.
- Legacy party membership can arrive before character selection. The server
  dispatches `PlayerJoinEvent` as part of authentication, and its existing
  `PartyService` may auto-join a connection and send party info before that
  player's server-owned character enters the world. This audit leaves that
  backend and its social protocol unchanged.
- `NotifyPlayerList` and `NotifyPlayerJoined` presence are based on
  `PlayerEnterWorldEvent`, emitted after the server completes character
  assignment. Angular's `playerConnected`/`playerDisconnected` callbacks update
  the legacy player list; they are presence data, not local character-session
  transitions.
- The remaining visible legacy surface is the party HUD. It used to flash on
  transport connection when auto-hide was enabled, and party-info updates could
  also flash it before world entry. Its visibility and auto-hide timer now wait
  for the native `inWorld` session state, which is emitted only after the
  matching server `NotifyCharacterEnteredWorld` response. The user's always-show
  preference remains effective while in-world.

## Result

There is no player-manager popup auto-open path after the U10 root-menu removal.
The connection-time party HUD assumption was real and is now gated by confirmed
character world entry. No local character data or selection state is inferred
from player presence, party membership, or transport connection.
