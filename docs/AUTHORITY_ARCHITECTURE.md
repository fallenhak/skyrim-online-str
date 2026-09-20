# Authority Layer

## Why this exists

Upstream Skyrim Together Reborn currently uses `PartyService` for both social grouping and world-authority decisions. That coupling is incompatible with a persistent RP server where core replication must work even when players are not members of a party.

The first migration step is intentionally behavior-preserving: move authority checks behind `AuthorityService` while still delegating to the existing party rules internally.

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
  who is connected
  which entities/events each client should receive

Persistence
  server-owned character and world data
```

## Migration rule

Do not remove `PartyService` until every non-social caller has moved behind a replacement abstraction.

The initial `AuthorityService::CanClaimActor` implementation intentionally preserves the old party-leader behavior. A later milestone will replace that implementation with proximity/interest/server policy without changing `CharacterService` again.
