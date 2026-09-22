# Bounded architect-directed correction — 2026-09-22

Parent architect-review capture: `9ebb61fb5e8e3aaf54faebe1ca0f2a278076cbfc`.

This receipt records the explicitly authorized correction of the retained
production L03 and U02 worktrees. It does not approve, retry, block, or advance
any lane.

## Production guard

- `GLOBAL: PAUSED`; `orchestrator.service` was `active/enabled`.
- `healthcheck.timer` was `active/enabled`.
- The process scan found no `codex` or development worker process.
- The control plane remained `VALID@@.
- `skyrim-dev review-status` remained pending for C04, A04, L03, and U02.
- No supervisor runtime code, roadmap state, control-plane state, combat lane,
  or authority lane was changed.

## L03 — ESL namespace correction

Protected branch and HEAD:

```text
parallel/population-loader
52c97ba4d5da993e6ef2fa4bdf398f13c22b456a
```

The correction-specific delta touched only these three files; the pre-existing
L03 changes in `Record.h` and `TESFile.*` were retained byte-for-byte:

- `Code/components/es_loader/ESLoader.cpp`
- `Code/tests/ActorPopulationTests.cpp`
- `docs/ACTOR_POPULATION.md`

`GetAuthoritativePluginType` now promotes a server-readable TES4 ESL flag to
the light namespace without allowing a readable header to downgrade `.esl`.
The resulting namespace cases are:

| Filename/header | Namespace |
| --- | --- |
| `.esl` with or without ESL bit | light |
| `.esp` with ESL bit | light |
| `.esp` without ESL bit | standard |
| `.esm` with ESL bit | light |
| `.esm` without ESL bit | master/standard as already modeled |
| malformed/unreadable header | reject header metadata and use server filename fallback |

The test correction now covers all six valid filename/header combinations,
metadata-only loading with no full record indexing, and malformed/truncated
TES4 headers for:

- a truncated header;
- a wrong first record type; and
- a TES4 `dataSize` extending beyond EOF.

Malformed headers are rejected and do not become more trusted than valid
metadata. Unknown actor classification and humanoid/creature policy were not
changed.

Final L03 status remained exactly:

```text
 M Code/components/es_loader/ESLoader.cpp
 M Code/components/es_loader/Records/Record.h
 M Code/components/es_loader/TESFile.cpp
 M Code/components/es_loader/TESFile.h
 M Code/tests/ActorPopulationTests.cpp
 M docs/ACTOR_POPULATION.md
```

Validation:

- `git diff --check`: PASS (`rc=0`).
- Standalone strict C++20 namespace/header probe: PASS
  (`L03_CPP20_NAMESPACE_PROBE=PASS`), including all valid and malformed cases.
- Production-source syntax compilation was attempted but the VDS lacks
  `TiltedCore/Filesystem.hpp`.
- `xmake` is unavailable and GTest/Catch2 headers are absent, so
  `ActorPopulationTests` could not run. This is a validation gap only.

## U02 — documentation recovery

Protected branch and HEAD:

```text
parallel/character-ui
a473531ad16a82cecc8a4cdecc460934ba7efcfa
```

Only the two already-intended staged documentation paths remain:

```text
M  docs/CHARACTER_UI_AUDIT.md
A  docs/CHARACTER_UI_STATE_MACHINE.md
```

Corrections:

1. Removed the extra EOF blank line from `CHARACTER_UI_STATE_MACHINE.md`.
   The file now ends with the final period followed by exactly one newline.
2. Rephrased the historical recovery note in `CHARACTER_UI_AUDIT.md`.
   It now states that current Git status has no `STATE.md` deletion and that
   the retained U02 documentation paths are the only current paths.

Validation:

- working-tree `git diff --check`: PASS (`rc=0`);
- cached `git diff --cached --check`: PASS (`rc=0`);
- working-tree diff after staging: empty;
- cached paths: exactly the two intended U02 documentation paths;
- no present-tense `STATE.md` deletion claim remains.

The accepted server-owned character list, selection authority, native snapshot
application, `CharacterReadyRequest`, player assignment, `InWorld` gate, and
no-local-store/no-protocol-change architecture were not changed.

## Protected lanes and mutation boundary

```text
combat    HEAD 500bf5ea5e04341f565776ed5263119c1cf06893
authority HEAD b8fc40415fceee88ae6424d25bd68a2a0ddeb70a
population HEAD 52c97ba4d5da993e6ef2fa4bdf398f13c22b456a
ui        HEAD a473531ad16a82cecc8a4cdecc460934ba7efcfa
```

Combat and authority were untouched. No development branch was committed,
pushed, merged, reset, cleaned, checked out, restored, or discarded. No
integration branch was created, and no lane decision was changed.
