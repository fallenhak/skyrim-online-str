# Combat Authority Readiness

This document defines the current combat boundary and the smallest safe future
attribution model. The server accepts bounded hit observations, but does not
apply hit damage, award XP, grant loot, or establish kill attribution.

## Current signal flow

- `Actor::HookDamageActor` can observe a local Skyrim hit as `HitterId` and
  `HitteeId`, and local health changes are emitted as signed deltas.
- `CombatService::OnHitEvent` and its target-update path are currently disabled
  (`#if 0`); there is no active client hit producer. The server accepts a
  separate observation request only after owner, target lifecycle, trusted
  population, cell, and replay validation.
- Projectile launches are currently visual/relay messages. They now require
  the local shooter ownership epoch, server current-owner validation, finite
  numeric input, and a matching remote incarnation before launch.
- Health changes are signed observation deltas. The server accepts them only
  from the current owner and matching epoch, applies them to the canonical
  current health value, and relays the accepted delta. Only an actual decrease
  in that canonical value emits an internal signal; the signal carries target
  server ID and lifecycle generation, not the submitted delta.
- Death state is reported by the current simulation owner with its epoch. The
  server records and relays state changes while ignoring duplicate reports. A
  trusted Creature's accepted alive-to-dead transition emits an internal event
  only when its canonical character flags also show that it is not a player,
  mount, or player summon. The event contains its server ID and lifecycle
  generation only; the sender is not treated as a killer identity.

## Authority boundaries

The server can currently establish:

- the sender's connection and player;
- the current owner and ownership epoch for a server entity;
- the server-only `ActorLifecycleComponent` generation for the current
  lifecycle/incarnation, independent of EnTT ID reuse;
- the server-only `ActorPopulationIdentityComponent`, including whether the
  target is a trusted creature rather than an unknown/client-claimed actor;
- the current character flags needed to exclude players, mounts, and summons;
- whether the accepted health/death observation is current and finite.

The server cannot currently establish from a client hit claim alone:

- that the claimed attacker actually caused the target's damage;
- the authoritative damage amount, timing, line of sight, or collision;
- an attribution through a projectile/effect chain after ownership changes;
- an XP/reward contribution or a valid PvP exclusion solely from a form ID.

Therefore accepted hit reports remain pending until a canonical health
decrease for the same target lifecycle is accepted. That association does not
prove the report caused the decrease and is not proof of a kill, XP, loot, or
rewards.

## Phase I — Validated hit-observation protocol

### Client producer review (C08)

The current client producers in `Actor::HookDamageActor` and
`MagicTarget::HookAddTarget` still create local `HitEvent` values from Skyrim
form IDs. Client `LocalComponent` and `RemoteComponent` entries can map a
currently known actor to its server ID and non-zero ownership epoch, but that
does not provide the target lifecycle generation required by the observation
DTO. `ActorLifecycleComponent` is server-only, and neither spawn nor ownership
messages send its generation. Since a server entity ID is not a substitute for
that lifecycle token after entity reuse, the producer cannot safely identify a
target incarnation for a network observation yet.

Accordingly, this review does not enable or add a network hit producer. The
existing `CombatService::OnHitEvent` target-update path remains disabled. A
future producer needs a server-verifiable way to bind the target to its current
lifecycle before it can submit an observation; current owner epochs alone do
not provide that binding.

The server-side `CombatHitObservationRequest` contains attacker and target
server entity IDs, the attacker ownership epoch, the target lifecycle
generation, and a replayable observation ID. It contains no persistent
`CharacterId`, damage, classification, or kill claim. The handler resolves the
attacker from canonical components and checks that the sender is in-world, is
the current owner at the requested nonzero epoch, and has the same
server-resolved persistent identity as the character selected in that session.
It then resolves the target and checks its current lifecycle, trusted Creature
identity, and canonical cell range before consulting the replay cache.

An accepted request is appended to a fixed 1024-entry pending FIFO. A full FIFO
rejects new requests and retains existing entries. The server assigns the
observation tick. After an accepted canonical health decrease, at most one
pending observation for that target server ID and lifecycle generation is
correlated and forwarded as a server-internal event. Stale generations for the
same entity ID are discarded. The match contains no client damage magnitude
and does not prove that the observation caused the decrease. The handler does
not apply damage, mutate death state, record contribution, award XP, or grant
loot. Client `HitEvent` production remains disabled: the target lifecycle
generation is server-only and is not yet sent in spawn or ownership messages,
so ordinary clients currently have no producer that can populate that field
correctly.

Later correlation work must use the accepted request's bounded, replayable
identity rather than trusting a client-provided persistent character ID:

- attacker server entity ID and attacker ownership epoch;
- target server entity ID and target lifecycle generation/epoch;
- a bounded observation/event ID;
- a server-assigned observation tick for ordering diagnostics;
- optional weapon/projectile/effect identity only after server-side form and
  classification checks.

The server must resolve the sender to the current owner, then resolve the
attacker server ID to the authoritative ECS entity and its persistent
`CharacterId`. The client must not choose that `CharacterId`. The target server
ID must resolve to a canonical entity with trusted creature classification;
self-hits, PvP targets, unsupported target classes, missing entities, and stale
epochs must be rejected by explicit policy. A bounded replay cache must reject
duplicate observation IDs and stale entity incarnations. The accepted
observation must be correlated with canonical health/death changes before any
future contribution is recorded. No client-provided XP amount or reward amount
should be authoritative.

`CombatObservationReplayCache` provides a bounded replay window. It keys an
observation ID by attacker server entity ID and ownership epoch, plus target
server entity ID and lifecycle generation. It retains a fixed FIFO window
(1024 entries by default); a key can be considered new again after eviction.
The handler validates the current attacker and target first, then consults the
cache immediately before accepting the observation. Replay identity excludes
the server-assigned observation tick.

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

- No client hit producer was enabled.
- No client hit claim is treated as proof of damage or a kill.
- No damage attribution, XP, inventory, party, session, or reward system was
  added.
- No PartyService or party-leader state is used as combat authority.
