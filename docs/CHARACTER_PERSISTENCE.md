# Character persistence foundation

The server database is the authoritative persisted source for character state. The local Skyrim save is not intended to remain authoritative when character selection, loading, and spawning are implemented in later milestones.

## V1 architecture

The persistence dependency direction is:

```text
Gameplay/session systems
        ↓
CharacterSaveService
        ↓
Persistence::CharacterRepository
        ↓
Persistence::Database
        ↓
SQLite

PersistenceService owns the repository and database instances used by the world.
```

`CharacterRecord` is a persistence-only model. It is deliberately separate from live ECS components, network messages, and `CharacterService` state. `CharacterSaveService` is the gameplay boundary for V1 runtime save-back: it reads the live persistent player ECS entity and calls the narrow repository update; gameplay code does not use raw SQLite. The persistent ECS component carries the owner profile captured from the trusted, owner-scoped assignment record and is not serialized to clients.

Runtime save-back writes only:

- WorldSpace and Cell.
- PositionX, PositionY, and PositionZ.
- Current Health, Magicka, and Stamina.
- `updated_at`.

The autosave interval defaults to 30 seconds and is controlled by `Persistence:uAutosaveIntervalSeconds`. A value of `0` disables periodic saves; positive values below five seconds use a five-second effective interval. A final save is attempted from `PlayerLeaveEvent` while the persistent ECS entity still exists, before normal character cleanup. The disconnect path uses ECS location/vitals rather than the session or the `Player` cell, which may already have been cleared during teardown.

Runtime save-back never modifies `Name`, `Race`, `Sex`, or `Level`, and it does not persist inventory/equipment, skills, perks, XP, appearance, or actor max/permanent values. Level remains owned by the existing player-level path; future progression persistence is intentionally outside this milestone.

Invalid runtime state is rejected without a database write. Repository exceptions are caught at the save service boundary and logged; the live entity is left intact so a later autosave or disconnect attempt can retry. V1 reads the server ECS actor-value map; hardening actor-value authority against client-owned gameplay updates remains future work.

## Schema

Schema version 1 contains:

- `schema_version`, with one current version row.
- `characters`, with a database-generated `INTEGER PRIMARY KEY AUTOINCREMENT` character ID.
- A profile-friendly text `owner_profile_id`, which is intentionally independent from transient connection or network player IDs.
- Name, sex, level, health, magicka, stamina, position, and created/updated Unix timestamps.
- Skyrim `GameId` values stored as separate `mod_id` and `base_id` columns for race, worldspace, and cell. This avoids persisting a client-local raw runtime form ID.

Migrations run inside a transaction. Future schema changes should append a new version step instead of changing the meaning of an existing version.

## Database location

The server setting `Persistence:sDatabasePath` controls the SQLite path. Its default is `Data/SkyrimTogetherServer.db`, relative to the server working directory. The parent directory is created before SQLite opens the file. This follows the existing server convention of using the repository/installation-relative `Data/` directory for server data.

## Deliberately out of scope

This milestone does not implement character selection UI, authentication, network messages, inventory/equipment/perks/skills/XP persistence, creature/quest/dungeon persistence, or future progression authority.
