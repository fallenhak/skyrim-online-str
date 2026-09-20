# Combat Authority Readiness

This document defines the current combat boundary and the smallest safe future
attribution model. It does not add a hit protocol, XP, loot, or combat reward
behavior.

## Current signal flow

- `Actor::HookDamageActor` can observe a local Skyrim hit as `HitterId` and
  `HitteeId`, and local health changes are emitted as signed deltas.
- `CombatService::OnHitEvent` and its target-update path are currently disabled
  (`#if 0`); there is no active client hit producer or server hit-claim
  handler.
- Projectile launches are currently visual/relay messages. They now require
  the local shooter ownership epoch, server current-owner validation, finite
  numeric input, and a matching remote incarnation before launch.
- Health changes are signed observation deltas. The server accepts them only
  from the current owner and matching epoch, applies them to the canonical
  current health value, and relays the accepted delta.
- Death state is reported by the current simulation owner with its epoch. The
  server records and relays the state, but the sender is not treated as a
  killer identity.

## Authority boundaries

The server can currently establish:

- the sender's connection and player;
- the current owner and ownership epoch for a server entity;
- the server entity's current lifecycle/incarnation;
- whether the target is a known character and, where classification is
  available, whether it is a creature rather than a player/mount/summon;
- whether the accepted health/death observation is current and finite.

The server cannot currently establish from a client hit claim alone:

- that the claimed attacker actually caused the target's damage;
- the authoritative damage amount, timing, line of sight, or collision;
- an attribution through a projectile/effect chain after ownership changes;
- an XP/reward contribution or a valid PvP exclusion solely from a form ID.

Therefore the current safe rule is to accept only owner/epoch-bound state
observations and to avoid deriving XP, loot, or rewards from them.

## Future validated observation shape

If combat attribution is added later, the request should identify an event with
bounded, replayable identity rather than trusting a client-provided persistent
character ID:

- attacker server entity ID and attacker ownership epoch;
- target server entity ID and target lifecycle generation/epoch;
- a per-attacker observation ID;
- a client tick or bounded observation timestamp for ordering diagnostics;
- optional weapon/projectile/effect identity only after server-side form and
  classification checks.

The server should resolve the sender to the current owner, resolve both ECS
entities, reject self/PvP/unsupported target classes according to an explicit
policy, and correlate the observation with canonical health/death changes. A
bounded replay cache must reject duplicate observation IDs and stale entity
incarnations. No client-provided `CharacterId`, XP amount, or reward amount
should be authoritative.

## Contribution and transfer implications

An attacker can disconnect or lose ownership before a target dies. Any future
contribution record must therefore store the resolved persistent character ID
only after server validation, while retaining the target server identity and
incarnation. Ownership transfer must invalidate stale attacker/target epochs
without retroactively granting a client authority to rewrite the record.

The record must expire, be bounded per target, and be consumed once at death.
Disconnect alone must not be interpreted as death or as proof of contribution.
Reward eligibility remains a later policy decision and is not implemented by
this milestone.

## Deliberate non-changes

- No network hit producer was enabled.
- No client hit claim is trusted by the server.
- No damage attribution, XP, inventory, party, session, or reward system was
  added.
- No PartyService or party-leader state is used as combat authority.
