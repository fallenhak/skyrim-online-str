# Authority Layer

## Why this exists

Upstream Skyrim Together Reborn currently uses `PartyService` for both social grouping and world-authority decisions. That coupling is incompatible with a persistent RP server where core replication must work even when players are not members of a party.

The migration began by moving authority checks behind `AuthorityService`. Actor ownership has now moved to a server-coordinated handoff model: party leadership no longer grants the right to take ownership of an already-managed actor.

## Current upstream coupling

At baseline `589b5f5b16bbfe61f5a1840068f9a5207e9a8b16`:

- server `CharacterService` allows certain ownership claims only to the party leader;
- client `CharacterService` uses party leadership when deciding whether to drive actors;
- weather authority follows the party leader;
- player/health/quest update paths contain party-membership gates.

## Target split

```text
PartyService
  social grouping only
  invites / membership / optional party UI

AuthorityService
  actor ownership eligibility
  world-state authority
  authority transfer policy

Presence / Interest Management
  who is visibly in the persistent world
  which entities/events each client should receive

Persistence
  server-owned character and world data
```

## Migration rule

Do not remove `PartyService` until every non-social caller has moved behind a replacement abstraction.

Actor authority policy now follows these rules:

- the client that first registers an unmanaged actor becomes its initial owner;
- party leadership does not allow a client to steal ownership from the current owner;
- when an owner relinquishes an actor or becomes unavailable, the server's existing `TransferToNextOwner` path selects another eligible in-range player;
- a later milestone should add explicit orphan/stale-owner recovery and stronger interest-management policy.

Weather authority is still temporarily backed by party state and is the next authority subsystem that must be made independent.

## Persistent-world presence boundary

Authentication creates a transport connection and a `Player` record, but it does not make that player globally visible. `PlayerJoinEvent` remains a connection-level legacy event for calendar, party, server-list, and scripting integrations.

`PresenceService` is the sole publisher of global `NotifyPlayerList`, `NotifyPlayerJoined`, and `NotifyPlayerLeft` messages. Its visible set begins only on `PlayerEnterWorldEvent`, after the persistent character entity has been created and the session has entered `InWorld`. It uses the `Player`'s persisted name, level, and cell/worldspace at that point.

Disconnecting before world entry produces no global leave notification. An in-world disconnect removes the player from the explicit presence set and publishes exactly one leave notification to the remaining in-world players. Presence lists never iterate unfiltered `PlayerManager` entries.

On the client, transport connection and persistent-world presence are separate states. Remote presence messages may be cached before local world sync, but `AuthorityService` has no world-authority source until `CharacterWorldSyncStartedEvent` confirms that the local persistent character is in-world. The transitional authority policy then chooses the lowest ID among the local in-world player and announced remote in-world players.
