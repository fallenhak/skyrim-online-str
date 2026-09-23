# Skyrim Online STR

[![Build Windows](https://github.com/fallenhak/skyrim-online-str/actions/workflows/windows.yml/badge.svg)](https://github.com/fallenhak/skyrim-online-str/actions/workflows/windows.yml)
[![Build Linux](https://github.com/fallenhak/skyrim-online-str/actions/workflows/linux.yml/badge.svg)](https://github.com/fallenhak/skyrim-online-str/actions/workflows/linux.yml)

**Skyrim Online STR** is an active fork of Skyrim Together Reborn / Tilted Online focused on turning Skyrim multiplayer from traditional co-op into a persistent RP/MMO-style platform.

> **Development status:** active development. This repository is not currently a finished end-user release.

## Vision

The target player experience is:

1. Launch Skyrim and connect to a server.
2. Authenticate.
3. Open Character Select.
4. Choose a server-owned persistent character.
5. Enter the shared multiplayer world.
6. Ordinary humanoid NPC population is suppressed so players can fill humanoid roles.
7. Trusted creatures, monsters, animals and undead remain as PvE population.
8. Dungeons and encounter populations can reset under server-controlled renewable-world rules.
9. Disconnecting and reconnecting restores the same server character and a coherent world state.

Skyrim Together Reborn remains the synchronization foundation, but single-player assumptions around characters, NPC population, persistence and authority are progressively being replaced with server-owned multiplayer systems.

## Core architecture

The project follows a few strict rules:

- Persistent player characters are server-owned.
- Account and character identity are server authority.
- Local Skyrim save data is bootstrap/shell state, not persistent multiplayer authority.
- Clients may continue to run Skyrim runtime and AI where necessary, but the server owns multiplayer identity, lifecycle, persistence and trust decisions.
- Ordinary humanoid population is suppressed by policy.
- Trusted creature/monster/undead population remains.
- Unknown actor classification is never guessed into a trusted creature.
- Respawned creatures receive a fresh lifecycle/incarnation.
- Stale packets from an older incarnation must not affect a newly spawned actor.
- Client-supplied XP, rewards, damage, kill attribution, persistent ownership and population classification are never accepted as authoritative merely because a client sent them.
- Security and trust correctness take priority over feature velocity.

We deliberately do **not** attempt to invent fake server-side Skyrim AI.

## Current milestone — M01 Core World

The current milestone is focused on the first complete persistent multiplayer world loop:

```text
Launch Skyrim
    ↓
Connect / authenticate
    ↓
Character Select
    ↓
Choose server-owned character
    ↓
Apply character snapshot
    ↓
Enter shared world
    ↓
Humanoid NPC population suppressed
    ↓
Multiple players coexist
    ↓
Trusted creatures remain and synchronize
    ↓
Validated creature combat / death / lifecycle
    ↓
Renewable encounter reset
    ↓
Disconnect / reconnect
    ↓
Same persistent character restored
```

Current engineering work includes:

- Character entry and Character Select UI
- Persistent character/session foundations
- Humanoid suppression and actor classification
- Creature lifecycle and combat authority
- Interaction/authority hardening
- Renewable dungeon/encounter foundations
- Persistence and reconnect validation

M01 is **not** considered complete just because implementation queues are exhausted. Final acceptance requires reviewed Windows runtime evidence with at least two Skyrim clients, including reconnect and renewable encounter-reset scenarios.

## Roadmap

### M01 — Core World

Establish the trusted shared-world foundation: persistent character entry, humanoid suppression, trusted PvE population, creature lifecycle/combat, reconnect and renewable encounters.

### M02 — Character Gameplay

Expand persistent character gameplay with systems such as character creation, progression, death/respawn and persistent inventory/equipment behavior.

### M03 — Persistent World

Build broader persistent-world systems such as trading, economy and persistent world objects/containers.

### M04 — RP & Social

Add RP-oriented systems such as guilds/organizations, factions, social features and admin/GM tooling.

### M05 — Production Hardening

Focus on abuse resistance, PvP rules, crash/reconnect reliability, performance/load testing, modlist compatibility and deployment/update hardening.

## Development model

Development happens in isolated workstreams and is integrated through review rather than by allowing unrelated work to modify the same branch.

Important project branches may be owned by active workstreams. Contributors should prefer their own feature branches and Pull Requests instead of pushing directly into active development lanes.

Cross-workstream integration is explicit and reviewed. Queue completion alone is not treated as product acceptance.

## Contributing

Contributions are welcome, but this fork is undergoing substantial architectural changes.

Before implementing a large feature:

1. Inspect the current repository and recent branch history.
2. Check whether the same area is already being actively developed.
3. Create a focused feature branch.
4. Keep the change bounded to one responsibility.
5. Run relevant builds/tests.
6. Open a Pull Request instead of merging directly.

For code style, follow the existing repository conventions and [CODE_GUIDELINES.md](./CODE_GUIDELINES.md).

When contributing to security-, authority-, persistence- or protocol-sensitive systems, avoid inventing new authority semantics without understanding the existing architecture.

## Building

The project retains the existing Tilted Online / Skyrim Together build structure and uses xmake across the native codebase.

Useful repository areas include:

- [`Code/client/`](./Code/client) — Skyrim client implementation
- [`Code/server/`](./Code/server) — game server implementation
- [`Code/common/`](./Code/common) — shared native code
- [`Code/encoding/`](./Code/encoding) — multiplayer message definitions
- [`Code/admin/`](./Code/admin) — admin application
- [`Code/admin_protocol/`](./Code/admin_protocol) — admin protocol
- [`Code/skyrim_ui/`](./Code/skyrim_ui) — Angular/TypeScript in-game UI
- [`Code/tests/`](./Code/tests) — native tests
- [`Code/immersive_launcher/`](./Code/immersive_launcher) — launcher
- [`Code/tp_process/`](./Code/tp_process) — CEF overlay worker

The repository's GitHub Actions workflows build both Windows and Linux targets.

## Upstream

This project is built on the work of the **Skyrim Together Reborn / Tilted Phoques** project and the Tilted Online codebase.

The upstream project provided the multiplayer synchronization foundation that this fork is extending toward a persistent server-owned world model.

Upstream resources:

- [Skyrim Together Reborn](https://github.com/tiltedphoques/TiltedEvolution)
- [Tilted Online Wiki](https://wiki.tiltedphoques.com/tilted-online/)

## License

[![GNU GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.en.html)

This project remains licensed under the **GNU General Public License v3.0**, consistent with the upstream codebase.

See [LICENSE](./LICENSE) for the complete license text.
