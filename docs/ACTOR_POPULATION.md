# Actor population classification

The server-side actor population foundation resolves plugin data before making a
classification decision. For a normal placed actor, the authority chain is:

```text
network ReferenceId (server ModId + client BaseId)
        -> authoritative server plugin identity / load-order prefix
        -> server ACHR form ID
        -> ACHR NAME (base object)
        -> NPC RNAM
        -> resolved RACE record / EDID
        -> ActorPopulationPolicy
        -> Player / HumanoidNpc / Creature / Unknown
```

The network `ModId` is resolved through the server's own mod identity mapping.
For standard plugins, only the client BaseId low 24 bits are used with the
server's standard load-order prefix. For light plugins, only the client BaseId
low 12 bits are used with the server's authoritative `FE` namespace and light
load-order ID. A client-supplied high prefix never overrides the server's
plugin identity, and an unknown or mismatched network mod identity remains
unresolved.

`ACHR` records are indexed by their resolved server form IDs. Their `NAME`
chunk is parsed with `Chunks::ReadFormId`, so the base object is also resolved
against server load-order metadata. A normal placed reference that resolves to
an ACHR is the trusted identity used for classification; a client FormId or
LeveledNpcPickId is retained only as an untrusted diagnostic claim and cannot
override the server-resolved ACHR/NPC result.

`NPC::m_raceId` stores the resolved ESLoader form ID produced by the shared
`Chunks::ReadFormId` helper. It is not the unresolved 24-bit plugin-local RNAM
value. `RACE` currently stores only its editor ID (`EDID`), which is the stable
server-side identity used by the policy. The typed record maps use resolved
ESLoader form IDs; generic `m_allRecords` keeps its existing plugin-local key
behavior.

`ActorPopulationPolicy::ClassifyNpcBase` uses safe, non-mutating lookups for the
NPC and RACE records. A missing NPC, RNAM, RACE, editor ID, or policy rule returns
`Unknown`; unknown data is never treated as `Creature` or `HumanoidNpc`.
The player reference `GameId(0, 0x14)` is classified independently as `Player`.
NPC classification otherwise accepts a resolved server-side form ID, not a raw
client load-order/runtime identity. Modded identities must be resolved by the
server before calling that API.

Temporary references (`ModId == UINT32_MAX`) and client-observed leveled picks
are not trusted placed-actor identities. They may resolve to an NPC only for
diagnostics; when no trusted server ACHR is available, their authoritative
classification remains `Unknown`. The identity resolver exposes the source and
trust distinction so a later enforcement milestone can make an explicit policy
decision without treating client claims as server authority.

Race rules are explicitly configurable with `SetRaceClassification`. The
default rule set marks only these vanilla editor IDs as `HumanoidNpc`:
`NordRace`, `BretonRace`, `ImperialRace`, `RedguardRace`, `HighElfRace`,
`WoodElfRace`, `DarkElfRace`, `OrcRace`, `ArgonianRace`, and `KhajiitRace`.
Other and edge/modded races remain `Unknown` unless an explicit server-side
rule is added. Editor ID is currently the policy key, so duplicate editor IDs
across plugins are not disambiguated by this foundation; plugin identity can be
added later if the loader exposes it as a required policy key.

## Assignment gate

`ActorPopulationAssignmentPolicy` consumes the resolved identity and never
treats a client actor-base or leveled-NPC claim as authoritative. Players are
always allowed. When `Population:bEnableHumanoidAssignmentGate` is enabled,
trusted vanilla humanoid NPCs are rejected before managed-actor lookup or ECS
entity creation, while trusted creatures remain allowed. Unknown or untrusted
identities are allowed by default; setting
`Population:bAllowUnknownActorAssignments` to `false` rejects them instead.
Both settings are locked, startup-only server settings. The humanoid gate is
disabled by default and the unknown-identity allowance is enabled by default.

Humanoid rejection is sent to the client with the assignment cookie and an
explicit rejection reason. The client marks the local entity as
population-suppressed, preventing retry loops during the current connection.
Cancelled assignments are cleaned up instead.

For `kPopulationHumanoidDenied` only, the connected client also applies a
reversible local `DisableImpl()` to the exact placed reference. A separate
session-owned form-ID registry records only references that this client actually
disabled; it is independent of ECS lifetime because the disable can itself
cause `ActorRemovedEvent`. If the reference reappears during the same
connection, it is marked suppressed again and re-disabled without another
assignment request. Unknown-denial responses never trigger physical disable.

On disconnect, the registry is drained and cleared before each owned reference
receives `EnableImpl()`. Already-disabled, temporary, deleted, unresolved, and
player references are never claimed or restored. Restoration is best effort
within the current Skyrim process. `Delete()` is never used for placed
humanoids; a process crash can prevent runtime restoration, and suppression is
not persisted to the character database.

## Startup activation

`Population:bEnableActorRecordLoading` is a locked, startup-only setting and
defaults to `false`. The RNAM/RACE parser remains available, but full server
plugin record loading is an explicit opt-in for actor-population
classification. This preserves the upstream behavior from
`0d942fc55b19592aa5395a6a86a7700e3276309b`, where full ESLoader parsing was
disabled while load-order metadata remained available for ModPolicy.

With the setting disabled, `World` still loads the load order and gives
`ActorPopulationPolicy` an empty `RecordCollection`; NPC classification is
`Unknown` and `GameId(0, 0x14)` is still `Player`. Since the assignment gate is
also disabled by default, this preserves the existing assignment behavior.

Load-order metadata is kept in the file's declared order. Blank/comment lines,
UTF-8 BOMs, CR/LF endings, and surrounding whitespace are normalized; duplicate
or unsupported/unsafe plugin entries (including path-bearing or control-character
names) are ignored without consuming an ID.
For a readable plugin file, the server uses the TES4 header ESL flag
(`0x00000200`) to promote an `.esp` or `.esm` into the light-plugin namespace.
An `.esl` filename remains light even when the readable header omits that bit;
the header never downgrades it to a standard namespace. For a malformed or
unreadable header in an existing plugin file, the server skips that plugin
instead of publishing a standard or light namespace. Missing plugin files keep
the filename-derived load-order metadata, without trusting client-reported mod
kind. During opt-in indexing, a plugin with any master absent from the known
standard-master map is skipped before any of its records are indexed.
Light-plugin master prefix mapping is not implemented yet, so an ESL-flagged
`.esm` can load its own records but a dependent plugin that names it as a master
is skipped safely. When full record loading is enabled, absent plugin files are
warned about and skipped while the parsed metadata remains available.

With it enabled, `World`
asks ESLoader to parse server plugin files and build references. If the Data
directory, `loadorder.txt`, or record collection is unavailable, the server
logs the condition and the policy keeps NPC results `Unknown`.

This parser opt-in is not hardened for arbitrary modlists, is not production
ready filtering/enforcement, and does not change runtime actor population.
Do not enable it as a substitute for a later loader-hardening milestone.

This milestone completes the client presentation side of the assignment gate:
trusted `HumanoidNpc` identity is rejected by the server, then the client
applies session-scoped, reversible local suppression. It does not alter
creature authority, change interest management, or make client race
classification authoritative. Draugr, Falmer, vampires, and modded races
remain `Unknown` unless explicitly configured, and `kPopulationUnknownDenied`
still never physically removes an actor. Static or modlist-level population
cleanup may be added later.
