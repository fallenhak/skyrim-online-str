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
default rule set is empty. Editor ID is currently the policy key, so duplicate
editor IDs across plugins are not disambiguated by this foundation; plugin
identity can be added later if the loader exposes it as a required policy key.

## Startup activation

`Population:bEnableActorRecordLoading` is a locked, startup-only setting and
defaults to `false`. The RNAM/RACE parser remains available, but full server
plugin record loading is an explicit opt-in for actor-population
classification. This preserves the upstream behavior from
`0d942fc55b19592aa5395a6a86a7700e3276309b`, where full ESLoader parsing was
disabled while load-order metadata remained available for ModPolicy.

With the setting disabled, `World` still loads the load order and gives
`ActorPopulationPolicy` an empty `RecordCollection`; NPC classification is
`Unknown` and `GameId(0, 0x14)` is still `Player`. With it enabled, `World`
asks ESLoader to parse server plugin files and build references. If the Data
directory, `loadorder.txt`, or record collection is unavailable, the server
logs the condition and the policy keeps NPC results `Unknown`.

This parser opt-in is not hardened for arbitrary modlists, is not production
ready filtering/enforcement, and does not change runtime actor population.
Do not enable it as a substitute for a later loader-hardening milestone.

This milestone is classification data, identity resolution, and debug
diagnostics only. It does not filter or despawn actors, reject assignments,
alter creature authority, or change interest management. The resolver is
currently called from the character-assignment path for logging and does not
change assignment behavior. Draugr, Falmer, vampires, and modded races remain
policy decisions, not parser assumptions. A later milestone can use the
trusted classification result to gate humanoid NPC population after real
mod-list behavior has been inspected.
