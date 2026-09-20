# Actor population classification

The server-side actor population foundation resolves plugin data before making a
classification decision:

```text
server-loaded plugin records
        -> NPC RNAM
        -> resolved RACE record / EDID
        -> ActorPopulationPolicy
        -> Player / HumanoidNpc / Creature / Unknown
```

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

This milestone is classification data and policy only. It does not filter or
despawn actors, reject assignments, alter creature authority, or change interest
management. Draugr, Falmer, vampires, and modded races remain policy decisions,
not parser assumptions. A later milestone can use the classification result to
gate humanoid NPC population after real mod-list behavior has been inspected.
