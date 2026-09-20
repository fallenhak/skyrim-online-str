# Character persistence foundation

The server database is the authoritative persisted source for character state. The local Skyrim save is not intended to remain authoritative when character selection, loading, and spawning are implemented in later milestones.

## V1 architecture

The persistence dependency direction is:

```text
Gameplay/session systems
        ↓
PersistenceService
        ↓
Persistence::CharacterRepository
        ↓
Persistence::Database
        ↓
SQLite
```

`CharacterRecord` is a persistence-only model. It is deliberately separate from live ECS components, network messages, and `CharacterService` state. This milestone only initializes the service and repository; it does not connect persistence to login, player spawning, or automatic character creation.

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

This milestone does not implement character selection UI, authentication, network messages, inventory/equipment/perks/skills/XP persistence, creature/quest/dungeon persistence, or loading persisted state into Skyrim.
