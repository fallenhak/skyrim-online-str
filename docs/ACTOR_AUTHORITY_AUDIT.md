# Client-to-Server Actor Authority Audit

Tracking issue: #28
Scope: client messages and packet handlers that mutate, relay, or represent
actor/world state.

## Dispatch boundary

`GameServer::BindMessageHandlers` installs the generated gameplay handlers with
an explicit `SessionService::CanProcessGameplay(connection)` check. This means
the generated gameplay messages below are dispatched only for
`SessionState::kInWorld`. Authentication, character list/selection/ready, and
assignment are installed separately. `AssignCharacterRequest` is the one
pre-world exception, and its handler allows only the local player reference in
`kAwaitingPlayerAssignment`; `CharacterService` repeats the same distinction.

The client has a matching outbound policy, but the server check is the trust
boundary. A client-side send restriction is not treated as authority.

## Classification

- **A** — authority/session/epoch validation is present and semantics are
  appropriate.
- **B** — owner validation exists, but stale-incarnation/epoch protection is
  incomplete.
- **C** — legitimate non-owner interaction or social action needs a separate
  authorization model.
- **D** — client-observed/presentation state that is tolerated but is not
  server-canonical actor state.
- **E** — unsafe or missing authority validation.
- **F** — deprecated/inactive path.

## Message matrix

| Message / handler | State mutated or represented | Entity identifier | Current owner required? | Ownership epoch | Session / range validation | Class | Recommended action | Implemented this batch? |
|---|---|---|---|---|---|---|---|---|
| `AssignCharacterRequest` / `CharacterService` | Creates server actor entity and initial state | client reference, then server entity | assignment-specific | created by server | pre-world local-player exception; non-player requires InWorld | A/D | Keep server identity gate; continue reducing client-authoritative fields | No |
| `AssignObjectsRequest` / `ObjectService` | Creates provisional object entity; initial inventory/lock snapshot is not accepted as trusted state | client object form ID | no object owner; shared use | none | InWorld; new objects must fall within sender's reported cell/range; known objects must match stored cell and range | D/E | Add authoritative static-reference identity and location before establishing shared object state | A09 (initial client state discarded; server reference source unresolved) |
| `ClientReferencesMoveRequest` / `CharacterService` | Movement and action replay cache | map key is server entity ID | yes | per-entry current epoch | `OwnerView` requires a character entity and current owner; InWorld gate; finite/bounded payload validation; no range restriction on owner simulation | A | Keep epoch, entity-membership, and payload validation | A05 |
| `RequestActorValueChanges` / `ActorValueService` | Canonical actor values | server entity ID | yes | current epoch required | generated InWorld gate; no finite/value-key validation | B/E | Validate finite values and known keys; avoid insertion on malformed state | No |
| `RequestActorMaxValueChanges` / `ActorValueService` | Canonical permanent/max values | server entity ID | yes | current epoch required | generated InWorld gate; no finite/value-key validation | B/E | Validate finite values and known keys | No |
| `RequestHealthChangeBroadcast` / `ActorValueService` | Canonical current health plus broadcast | server entity ID | **yes** | **missing in baseline** | generated InWorld gate; no finite validation | E | Append epoch, require current owner, reject non-finite deltas, use non-inserting health lookup | Phase C |
| `RequestDeathStateChange` / `ActorValueService` | Canonical death state | server entity ID | yes | current epoch required | generated InWorld gate | A | Keep; death observation is not attacker attribution | No |
| `RequestFactionsChanges` / `CharacterService` | Canonical faction list | map of server entity IDs | yes | per-entry current epoch | generated InWorld gate; owner check; bounded faction entries with valid unique IDs; persistent players have no non-owner exception | A | Keep per-entity epoch and owner-only mutation | A06 |
| `DrawWeaponRequest` / `InventoryService` | Canonical weapon-drawn state | server entity ID | owner pointer only | missing | generated InWorld gate | B | Carry and validate current ownership epoch | No |
| `NewPackageRequest` / `CharacterService` | Broadcast package assignment to clients | server actor ID | no check | missing | generated InWorld gate; `SendToPlayersInRange` only validates origin existence | E | Require current owner and validate package identity | No |
| `RequestRespawn` / `CharacterService` | Owner appearance/death replay state, or observer spawn response | server actor ID | owner for mutation; non-owner request is a legacy observation path | missing | generated InWorld gate | B/C | Add an optional owner epoch; only owner+epoch may mutate, preserve observer response | No |
| `RequestOwnershipTransfer` / `CharacterService` | Releases current ownership and may update location | server entity ID | yes | current epoch required | range used for next owner; submitted location is trusted from owner | A/B | Keep owner/epoch check; audit movement/location trust separately | No |
| `RequestOwnershipClaim` / `CharacterService` | Transfers ownership | server entity ID | claimant must pass claim policy | expected epoch required | range and actor type checks; `AuthorityService::CanClaimActor` currently denies claims | A | Verify transfer tests; do not reintroduce party authority | No |
| `MountRequest` / `CharacterService` | Mount relation and mount ownership | rider + mount IDs | rider owner; mount epoch | both epochs required | both owner checks and range | A | Keep; verify mount/persistent-player protections | No |
| `ProjectileLaunchRequest` / `CombatService` | Projectile presentation with claimed shooter | arbitrary shooter ID | no check | missing | generated InWorld gate; origin entity is only used for range broadcast | E | Resolve shooter entity and require sender ownership/epoch without breaking player shots | Phase D candidate |
| `SpellCastRequest` / `MagicService` | Remote spell presentation/cast | caster ID and desired target ID | current caster owner | caster epoch required | generated InWorld gate; nonzero target must resolve to a character or object entity; target ownership is not required | A/D | Keep caster authority; preserve targetless/environmental casts and non-owner target interactions | A08 |
| `InterruptCastRequest` / `MagicService` | Remote cast interruption | caster ID | current caster owner | caster epoch required | generated InWorld gate; casting source is bounded | A/D | Bind to caster authority/incarnation | A08 |
| `AddTargetRequest` / `MagicService` | Applies remote magic effect presentation | target + optional caster IDs | target owner or explicit caster owner | current target epoch and optional caster epoch | generated InWorld gate; target/caster must resolve to owned character entities; finite magnitude; fan-out around target but no sender/spell range check | C/E | Preserve target-owner incoming/environmental reports and caster-owner reports; validate both endpoint incarnations | A07/A08 |
| `RemoveSpellRequest` / `MagicService` | Removes spell on remote actor | target ID | current target owner | target epoch required | generated InWorld gate; notification carries epoch and receivers require matching remote incarnation | A/D | Bind removal to the actor's current owner/incarnation | A08 |
| `RequestInventoryChanges` / `InventoryService` | Actor/object inventory contents | server entity ID | owner for actor; non-owner NPC interaction allowed | epoch checked | non-owner NPC requires in-range, non-player, non-persistent character; malformed item payloads are rejected; ownerless path requires a trusted object and sender proximity to its stored location | A/C/E | Preserve loot/pickpocket semantics; establish a server-authoritative object baseline before enabling object deltas | A02/A03/A09 |
| `RequestEquipmentChanges` / `InventoryService` | Actor equipment | server entity ID | sender must own a character | current nonzero epoch required | generated InWorld gate; target must have CharacterComponent and OwnerComponent; no range | A | Preserve owner-driven character equipment; reject object and other non-character inventories | A09 |
| `ActivateRequest` / `ObjectService` | Activation relay | object ID, cell, activator ID | activator must be sender-owned; shared object use is gated by object trust | request has no epoch | object must be known and trusted; supplied cell must match stored cell; sender and activator must be in stored range; open state is bounded | C/D | Keep provisional references closed until an authoritative static-reference source is reviewed; add an epoch only with a protocol change | A09 |
| `LockChangeRequest` / `ObjectService` | Lock state and peer relay for trusted references | object form ID + cell | no object owner check; shared use needs an authoritative outcome | none | object must be known; supplied cell must match stored cell; sender must be in stored range; provisional references and unvalidated client outcomes are rejected before mutation or relay | C/D | Establish a reviewed lock-outcome authority before applying client-reported results | A09 (all current client reports are rejected) |
| `ScriptAnimationRequest` / `ObjectService` | Animation presentation | server entity ID resolved from the local form ID | no; nearby interaction may be non-owner | none | source must be a registered NPC with form and cell components; sender must be in its tracked range; fan-out is range-limited; provisional objects are rejected | D/E | Preserve nearby NPC presentation; keep client-discovered objects closed until an authoritative identity and location source exists | A10 |
| `DialogueRequest` / `CharacterService` | Voice presentation | server actor ID | no; non-owner interaction can be legitimate | none | source must be a registered non-player character with form and cell components; sender must be in range; fan-out is range-limited | C/D | Preserve nearby non-owner dialogue; sound filename remains client-reported presentation | A10 |
| `SubtitleRequest` / `CharacterService` | Subtitle presentation | server actor ID | no; non-owner interaction can be legitimate | none | same registered-NPC and sender-range checks as dialogue; fan-out is range-limited | C/D | Preserve nearby non-owner dialogue; subtitle text/topic remain client-reported presentation | A10 |
| `PlayerRespawnRequest` / `PlayerService` | Own persistent-player respawn and gold loss | sender's server character | sender's own player | server resolves sender | generated InWorld gate | A | Keep sender-derived identity | No |
| `PlayerLevelRequest` / `PlayerService` | Player level presentation | sender player | sender's own player | n/a | generated InWorld gate; persistent players rejected | A/D | Keep persistent level server-controlled | No |
| `RequestPlayerHealthUpdate` / `OverlayService` | UI health percentage | sender player | sender-derived | n/a | generated InWorld gate | D | Keep non-canonical and validate finite percentage if UI abuse matters | No |
| `RequestWeatherChange` / `WeatherService` | Canonical shared weather | no entity | world authority | n/a | `AuthorityService` elects lowest InWorld player ID | A | Keep independent of parties | No |
| `RequestQuestUpdate` / `QuestService` | Legacy quest log relay | sender player | sender-derived | n/a | InWorld gate; feature disabled by constant | F | Do not enable without a separate quest authority design | No |
| party/map/chat requests | Social state or UI | player/party IDs | social authorization | n/a | service-specific | A/C | Keep separate from actor authority | No |

## Object discovery and interaction semantics

Object assignment accepts provisional references only when their submitted
cell/worldspace/grid coordinates fall within the sender's tracked range. The
server ignores client-supplied server IDs and does not seed canonical inventory
or lock state from the request. Responses mark that state untrusted, and clients
keep their own local container state instead of applying the unverified server
snapshot. Inventory deltas for provisional ownerless objects are rejected;
the shared-object path requires both a trusted baseline and sender proximity to
the object's stored location. No current lane source establishes that
baseline, so discovered object inventory is not synchronized as server state.

There is no usable authoritative reference-location source in this lane. The
server `World` loads full plugin records only behind the disabled-by-default
`Population:bEnableActorRecordLoading` setting. Even when loaded, ESLoader's
`REFR` parser records base-object/marker data but not parent cell or position,
and `ObjectService` does not consult it. A server-side reference ID/type check
alone would not validate the submitted location. This leaves provisional
reference identity and placement, and all discovered object state, unresolved
for a follow-up authority design. Range still relies on player cell/movement
reports and does not prove how the sender reached that location; see the
separate cell/teleport work.

The client producer is `TESObjectREFR::HookActivate`, which emits an
`ActivateEvent`; `ObjectService::OnActivate` maps its activator to a server
entity ID and sends `ActivateRequest`. Applying `NotifyActivate` on an observer
calls the same game activation hook and can produce a replay request, so a
nearby non-owner report is not a legitimate new interaction. The server now
requires a live sender-owned character entity and checks that both the sender
and activator are in the object's tracked grid range. The request has no
ownership epoch, so it cannot distinguish a delayed packet from the same
sender after an ownership release and reacquisition. `AssignObjects` creates
discovered references as provisional, and no current source establishes trusted
object state. As a result, activation relay for client-discovered objects is
currently suppressed, including ordinary nearby shared use. Keep this path
closed until the static-reference authority source receives separate review.

Lock changes remain client-observed: the producer reports the result after the
game's lock-change hook, but the server has no lock-picking simulation to prove
that result. The server validates object existence, stored cell, and sender
range, then rejects provisional object results before changing stored lock data
or notifying peers. The handler also rejects every report without an
independently validated outcome, even if a trusted baseline were added. There
is no current source that marks discovered objects as trusted, so lock relay
for client-discovered objects is currently suppressed. Keep this path closed
until both object-state and lock-outcome sources receive separate review.

## AddTarget semantics and authorization

`MagicTarget::HookAddTarget` emits effects from either side of an interaction.
When the caster is locally simulated, the caster owner reports effects applied
to other actors, including healing or buffs applied to a remote player. When
the target is locally simulated, its owner reports effects applied to that
actor; this includes caster-less effects and incoming PvP effects from a remote
caster. A caster-owner-only rule would drop the latter, while a target-owner-
only rule would drop the former.

`MagicService` now accepts a report when the sender currently owns the target,
or when a nonzero caster ID resolves to a character with an owner and the
sender currently owns that caster. A zero caster ID is the existing
caster-less sentinel and therefore requires target ownership. Both endpoints
must be canonical character entities with a live owner. Magnitude finite-value
validation was already present and remains in place. Reports now carry the
target epoch and, when a caster exists, its epoch; the server checks both
incarnations before applying the existing target-owner-or-caster-owner rule.
The caster-less sentinel still requires only the target-owner route and has no
caster epoch. This policy proves which side of the interaction the sender owns;
it does not prove that the client observed the claimed effect or that a spell
was in range.

Spell cast and interrupt requests require the caster's current owner and
ownership epoch. A nonzero desired spell target must resolve to a registered
character or object, but does not require target ownership; zero remains valid
for targetless/environmental casts. RemoveSpell is emitted only for the local
player actor, and now carries that actor's epoch. The server accepts only the
current owner, then recipients apply the notification only to a matching
remote incarnation.

These message layouts are not backward-compatible with clients that omit the
new ownership epoch fields. The client sends `BUILD_COMMIT` during
authentication, and the server rejects a client whose version does not exactly
match its own `BUILD_COMMIT`; clients and servers must therefore run the same
build when using these messages.

The stock client currently compiles script-animation capture behind
`OBJECT_ANIM_SYNC=0`; server-side source and range checks still apply to crafted
or custom-client requests.

`AssignObjectsRequest` creates client-discovered object entities with a
provisional form ID and location. Script-animation requests cannot relay from
these entities; there is no authoritative static-reference identity and
location source to validate them. Nearby registered NPC presentation remains
available to non-owners.

## High-confidence conclusions

1. Health is the clear canonical-state vulnerability: the server currently
   accepts a signed delta without proving the sender owns the actor incarnation,
   and uses `ActorValuesList[24]`, which can insert missing state.
2. Projectile launch trusts a client-selected shooter ID and can cause other
   clients to render a shot from an arbitrary server entity. This is a serious
   presentation/interaction spoofing issue, but its safe fix must preserve
   legitimate player and owner-controlled creature projectiles.
3. Package and some object paths remain relay or mutation surfaces without a
   complete actor authority proof. Script animation now resolves through a
   registered server entity and is limited to nearby NPC sources; provisional
   objects are rejected. Dialogue and subtitles require a nearby registered
   NPC. Their content is still client-reported presentation data. Spell cast,
   interrupt, AddTarget, and RemoveSpell now bind requests to the current actor
   incarnation. AddTarget preserves both caster-owned and target-owned
   environmental/incoming effects; magic range and effect observation remain
   client-reported and are not proven by the server.
4. `OwnerView` validates the current owner pointer, but it does not validate an
   ownership epoch by itself. Movement and faction updates now check the current
   epoch per actor. Draw-weapon state still has a stale-incarnation risk until
   its separate follow-up adds an epoch.
   Faction authority and malformed-payload assertions are in the `TPTests`
   target. This worker could not run them because `xmake` is unavailable, and
   the local tree contains no CI configuration or run output establishing
   whether the exact-SHA CI executed `TPTests`; focused faction test execution
   therefore remains unverified.
5. Inventory intentionally allows in-range non-owner NPC interaction. This is
   not equivalent to authority over a persistent player actor. `InventoryService`
   now uses a separate policy that rejects persistent players from the NPC
   exception, rejects ownerless non-object entities, and requires range for
   trusted ownerless objects. No current source establishes a trusted object
   baseline. Empty/zero-count, minimum signed
   count, non-finite item payloads, missing-item removals, over-removals, and
   stack-overflowing counts are rejected before mutation, while `Drop` and
   `UpdateClients` remain post-authorization notification controls. A blanket
   owner check would still break legitimate gameplay. The focused policy tests
   are included in the `TPTests` xmake target. The default test command is
   blocked before compilation because xmake cannot open the user-level
   detection cache at
   `C:\Users\kerim\AppData\Local\.xmake\cache\detect`. Retrying with the
   repository-local xmake global directory detects the installed MSVC
   toolchain, but xmake cannot update its dependency repository because the
   environment cannot acquire the required schannel credentials
   (`SEC_E_NO_CREDENTIALS`), so the test binary cannot be built or run here.

## Deliberately not fixed in the audit-only phase

- No mechanical epoch field was added to every message.
- No persistent inventory-authority redesign was attempted.
- No combat damage attribution or XP producer was enabled.
- No PartyService deletion or UI/social behavior was changed.
