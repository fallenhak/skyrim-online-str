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

Actor removal clears replay keys and pending observations where that server
entity was either attacker or target, and clears contribution records for all
target lifecycles using that entity ID. An owner-authorized respawn advances
the server lifecycle generation, resets its accepted-death marker, and applies
the same cleanup for both player and actor respawn requests before notifying
peers. Reusing an EnTT slot therefore cannot inherit combat state from the
removed or respawned actor. Contributions already correlated to another live
target remain attached to their server-resolved persistent CharacterId, as
described below.

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
correlated and forwarded as a server-internal event. Before selecting that
observation, stale target generations and observations whose attacker no
longer resolves to its current in-world owner at the recorded epoch are
discarded. Each accepted observation also captures the attacker's current
server-owned lifecycle generation. Correlation checks it again, so removal
and entity-ID reuse cannot rebind a pending report even if the ownership epoch
is recycled. A disconnected or transferred attacker therefore cannot consume a
later owner's canonical health decrease. The match contains no client damage
magnitude and does not prove that the observation caused the decrease. The
handler does not apply damage, mutate death state, record contribution, award
XP, or grant loot. Client `HitEvent` production remains disabled: the target
lifecycle generation is server-only and is not yet sent in spawn or ownership
messages, so ordinary clients currently have no producer that can populate
that field correctly.

The current correlation keeps the accepted request's bounded, replayable
identity through health matching, then re-resolves the attacker against its
current owner, in-world session, ownership epoch, and server-owned persistent
`CharacterId` before recording a contribution. The client does not choose that
`CharacterId`. Target eligibility comes from the canonical target's trusted
Creature classification and lifecycle generation. Self-hits, PvP targets,
unsupported target classes, missing entities, and stale epochs are rejected by
explicit policy. The contribution ledger is consumed only for the accepted
canonical Creature death transition; the death event carries contributor IDs,
not a client-selected killer. No client-provided XP or reward amount is
authoritative.

`CombatObservationReplayCache` provides a bounded replay window. It keys an
observation ID by attacker server entity ID, ownership epoch, and lifecycle
generation, plus target server entity ID and lifecycle generation. It retains
a fixed FIFO window (1024 entries by default); a key can be considered new
again after eviction.
The handler validates the current attacker and target first, then consults the
cache immediately before accepting the observation. Replay identity excludes
the server-assigned observation tick.

## Contribution and transfer implications

An attacker can disconnect or lose ownership before a target dies. An
observation still pending at that point is discarded when it fails current
owner, epoch, or in-world session validation. Once a canonical health decrease
has correlated an observation, the contribution ledger stores only its
server-resolved persistent CharacterId and the target server identity and
incarnation. A later disconnect or ownership transfer does not rewrite that
record, and a stale packet cannot replace its contributor identity.

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
