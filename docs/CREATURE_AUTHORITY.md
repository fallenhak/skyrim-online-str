# Creature Authority Readiness

This document records the creature lifecycle and authority boundary reviewed in
the overnight actor-authority hardening batch. It describes the current runtime
architecture; it does not introduce server-simulated Skyrim AI.

## Authority terminology

- The server owns the canonical ECS entity, its server ID, the current
  `OwnerComponent`, and the ownership epoch.
- The current owner is the client that is allowed to drive the actor simulation
  and report the currently supported actor mutations. This is not the same as
  the server simulating the actor's Skyrim AI.
- Other clients receive server relays and render/interpolate the remote actor.
- World authority is a separate concept used by weather/presence logic. It is
  not a claim right for an individual creature and is not derived from party
  leadership.

## Current lifecycle

1. **Local discovery** — `DiscoveryService::VisitForms` observes high-process
   references with a `NiNode`, plus the local player. New references become
   client ECS entries and are sent through `AssignCharacterRequest` after the
   gameplay session is active.
2. **Server identity and classification** — `CharacterService` resolves the
   reference/NPC identity and applies the server-side population assignment
   policy before creating a canonical entity. Humanoid suppression is rejected
   here; this path is not a client classification grant.
3. **Canonical entity and initial owner** — `CreateCharacter` creates the ECS
   entity and attaches `OwnerComponent` with the assigning player and a
   non-zero ownership epoch. It also attaches the server-only
   `ActorLifecycleComponent`, whose monotonic generation identifies this actor
   incarnation independently of the EnTT entity value and is never supplied by
   the client. The canonical entity also carries the
   `ActorPopulationIdentityComponent`, which stores the trusted population
   classification and resolved server form IDs. `CharacterComponent` records
   mount, summon, dragon, and death state; its client-provided base identity is
   not a combat authority source.
4. **Spawn publication** — `CharacterSpawnedEvent` serializes the canonical
   state, including server ID and ownership epoch, and sends it to eligible
   clients through the server range filter. A remote client creates or
   reconciles its remote actor against that incarnation.
5. **Owner simulation and observation** — the owner sends movement, actor
   value, health, death, projectile, package, and magic observations only with
   the current epoch. The server validates the sender against the ECS owner
   before mutating or relaying the state. The server remains an authority over
   acceptance and replication, not a replacement Skyrim AI.
6. **Ownership transfer** — relinquish, owner-unavailable, mount, and explicit
   claim paths validate the expected epoch and actor eligibility. On owner
   loss, `TransferToNextOwner` examines connected players, excludes the current
   owner and `InvalidOwners`, requires cell/range eligibility, and increments
   the epoch through `TransferOwnership`. A declined handoff is retained in
   `InvalidOwners` so the actor does not bounce between unloaded clients.
7. **Health and death** — health deltas and death-state changes are accepted
   only from the current owner with the current epoch. Clients match remote
   notifications to the same server ID and epoch before applying them. Death
   reporting identifies the simulation owner; it does not currently identify
   a killer or reward contributor.
8. **Disconnect, removal, and reappearance** — `GameServer::OnDisconnection`
   saves persistent player state, removes the player character, and queues
   ownership transfer for other owned entities. If no eligible owner remains,
   `CharacterRemoveEvent` broadcasts removal and destroys the server entity,
   including its lifecycle component. A later actor using a reused EnTT slot
   receives a new generation.
   Client discovery keeps an independent suppression/assignment registry so a
   temporary disappearance does not silently grant a new authority; a real
   reappearance is reconciled and assigned according to the current session.

## Special actor cases

- Player actors are not claimable by another player through the creature claim
  path.
- Mounts are protected from generic claims and use their rider/mount ownership
  checks for mount operations.
- Player summons are removed when their owner explicitly relinquishes the
  ownership transfer path; ordinary creature transfer is not used to keep a
  summon alive without its owner.
- Temporary references and population-suppressed references have separate
  lifecycle handling and must not be treated as persistent creature identity.

## Party and authority findings

`PartyService` remains a social service. No actor creation, transfer, health,
death, or disconnect authority decision currently reads party membership or
party leader state. `AuthorityService::CanClaimActor` deliberately returns
false for social-role claims; actual initial assignment and handoff live in
`CharacterService`. The transfer reason strings still contain historical
"party leader" wording, but that wording is diagnostic and is not an
authorization rule.

On the client, `AuthorityService::HasLocalActorAuthority()` is deliberately
false. Presence-derived world authority is used for world-scoped state and is
not an actor-ownership shortcut. No PartyService decoupling change was needed
for this audit.

## Readiness gaps

- The owner client still runs the Skyrim actor simulation; the server validates
  accepted observations rather than reproducing AI.
- Projectile and health messages are authority-bound observation relays, not a
  server-side damage or hit ledger.
- `AddTarget` now requires current ownership of either the target or its
  explicit caster, preserving caster-less and incoming effects through target
  ownership. The message has no ownership epochs or spell-range proof, so those
  remain separate hardening questions.
- Killer identity, contribution attribution, XP, loot, and reward eligibility
  are intentionally outside this milestone.
