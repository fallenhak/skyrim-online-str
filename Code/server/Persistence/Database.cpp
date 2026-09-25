#include <Persistence/Database.h>

#include <sqlite3.h>

#include <spdlog/spdlog.h>

#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Persistence
{
namespace
{
constexpr int kCurrentSchemaVersion = 6;

[[noreturn]] void ThrowSqliteError(sqlite3* apDatabase, const int aResult, const std::string_view acOperation)
{
    const char* pError = apDatabase ? sqlite3_errmsg(apDatabase) : "unknown SQLite error";

    std::string message(acOperation);
    message += ": ";
    message += pError;
    message += " (SQLite error ";
    message += std::to_string(aResult);
    message += ')';

    spdlog::error("[Persistence] {}", message);
    throw std::runtime_error(message);
}

void CheckBindResult(sqlite3_stmt* apStatement, const int aResult, const std::string_view acOperation)
{
    if (aResult != SQLITE_OK)
        ThrowSqliteError(sqlite3_db_handle(apStatement), aResult, acOperation);
}

void CheckStringLength(const std::string_view acValue)
{
    if (acValue.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("SQLite text value is too large");
}
} // namespace

void Database::DatabaseDeleter::operator()(sqlite3* apDatabase) const noexcept
{
    if (!apDatabase)
        return;

    const int result = sqlite3_close_v2(apDatabase);
    if (result != SQLITE_OK)
        spdlog::error("[Persistence] Failed to close SQLite database: {}", sqlite3_errstr(result));
}

void Database::StatementDeleter::operator()(sqlite3_stmt* apStatement) const noexcept
{
    if (!apStatement)
        return;

    const int result = sqlite3_finalize(apStatement);
    if (result != SQLITE_OK)
        spdlog::error("[Persistence] Failed to finalize SQLite statement: {}", sqlite3_errstr(result));
}

Database::Statement::Statement(sqlite3_stmt* apStatement, std::unique_lock<std::recursive_mutex>&& aLock) noexcept
    : m_lock(std::move(aLock))
    , m_statement(apStatement)
{
}

Database::Statement::~Statement() noexcept = default;
Database::Statement::Statement(Statement&&) noexcept = default;
Database::Statement& Database::Statement::operator=(Statement&& aOther) noexcept
{
    if (this == &aOther)
        return *this;

    m_statement.reset();
    m_lock = std::move(aOther.m_lock);
    m_statement = std::move(aOther.m_statement);
    return *this;
}

void Database::Statement::Bind(const int aIndex, const std::string_view acValue)
{
    CheckStringLength(acValue);
    CheckBindResult(
        m_statement.get(), sqlite3_bind_text(m_statement.get(), aIndex, acValue.data(), static_cast<int>(acValue.size()), SQLITE_TRANSIENT), "Failed to bind SQLite text value");
}

void Database::Statement::Bind(const int aIndex, const std::int64_t aValue)
{
    CheckBindResult(m_statement.get(), sqlite3_bind_int64(m_statement.get(), aIndex, aValue), "Failed to bind SQLite integer value");
}

void Database::Statement::Bind(const int aIndex, const double aValue)
{
    CheckBindResult(m_statement.get(), sqlite3_bind_double(m_statement.get(), aIndex, aValue), "Failed to bind SQLite real value");
}

bool Database::Statement::Step()
{
    const int result = sqlite3_step(m_statement.get());
    if (result == SQLITE_ROW)
        return true;
    if (result == SQLITE_DONE)
        return false;

    ThrowSqliteError(sqlite3_db_handle(m_statement.get()), result, "Failed to execute SQLite statement");
}

std::int64_t Database::Statement::ColumnInt64(const int aIndex) const noexcept
{
    return sqlite3_column_int64(m_statement.get(), aIndex);
}

double Database::Statement::ColumnDouble(const int aIndex) const noexcept
{
    return sqlite3_column_double(m_statement.get(), aIndex);
}

std::string Database::Statement::ColumnText(const int aIndex) const
{
    const auto* pText = sqlite3_column_text(m_statement.get(), aIndex);
    if (!pText)
        return {};

    return {reinterpret_cast<const char*>(pText), static_cast<size_t>(sqlite3_column_bytes(m_statement.get(), aIndex))};
}

Database::Transaction::Transaction(Database& aDatabase)
    : m_database(aDatabase)
    , m_lock(aDatabase.m_mutex)
{
    m_database.Execute("BEGIN IMMEDIATE TRANSACTION;");
}

Database::Transaction::~Transaction() noexcept
{
    if (!m_committed)
        m_database.RollbackNoThrow();
}

void Database::Transaction::Commit()
{
    m_database.Execute("COMMIT TRANSACTION;");
    m_committed = true;
}

Database::Database(const std::filesystem::path& acPath)
    : m_database(nullptr)
    , m_path(acPath)
{
    if (m_path.empty())
        throw std::invalid_argument("SQLite database path cannot be empty");

    if (m_path != std::filesystem::path(":memory:"))
    {
        const auto parentPath = m_path.parent_path();
        if (!parentPath.empty())
        {
            std::error_code error;
            std::filesystem::create_directories(parentPath, error);
            if (error)
            {
                const std::string message = "Failed to create SQLite database directory '" + parentPath.string() + "': " + error.message();
                spdlog::error("[Persistence] {}", message);
                throw std::runtime_error(message);
            }
        }
    }

    sqlite3* pDatabase = nullptr;
    const std::string path = m_path.string();
    const int result = sqlite3_open_v2(path.c_str(), &pDatabase, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    m_database.reset(pDatabase);

    if (result != SQLITE_OK)
        ThrowSqliteError(m_database.get(), result, "Failed to open SQLite database '" + path + "'");

    sqlite3_extended_result_codes(m_database.get(), 1);
    sqlite3_busy_timeout(m_database.get(), 5000);
}

Database::~Database() noexcept = default;
Database::Database(Database&& aOther) noexcept
    : m_database(nullptr)
{
    std::lock_guard<std::recursive_mutex> lock(aOther.m_mutex);
    m_database = std::move(aOther.m_database);
    m_path = std::move(aOther.m_path);
}

Database& Database::operator=(Database&& aOther) noexcept
{
    if (this == &aOther)
        return *this;

    std::scoped_lock lock(m_mutex, aOther.m_mutex);
    m_database = std::move(aOther.m_database);
    m_path = std::move(aOther.m_path);
    return *this;
}

void Database::Migrate()
{
    Execute("PRAGMA foreign_keys = ON;");
    Execute("CREATE TABLE IF NOT EXISTS schema_version (id INTEGER PRIMARY KEY CHECK (id = 1), version INTEGER NOT NULL);");

    int schemaVersion = 0;
    bool hasSchemaVersion = false;
    {
        auto statement = Prepare("SELECT version FROM schema_version WHERE id = 1;");
        if (statement.Step())
        {
            hasSchemaVersion = true;
            schemaVersion = static_cast<int>(statement.ColumnInt64(0));
        }
    }

    if (schemaVersion > kCurrentSchemaVersion)
    {
        const std::string message = "Database schema version " + std::to_string(schemaVersion) + " is newer than supported version " + std::to_string(kCurrentSchemaVersion);
        spdlog::error("[Persistence] {}", message);
        throw std::runtime_error(message);
    }

    Transaction transaction(*this);

    if (!hasSchemaVersion)
        Execute("INSERT INTO schema_version (id, version) VALUES (1, 0);");

    if (schemaVersion < 1)
    {
        Execute(R"sql(
            CREATE TABLE IF NOT EXISTS characters (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                owner_profile_id TEXT NOT NULL,
                name TEXT NOT NULL,
                race_mod_id INTEGER NOT NULL,
                race_base_id INTEGER NOT NULL,
                sex INTEGER NOT NULL,
                level INTEGER NOT NULL,
                worldspace_mod_id INTEGER NOT NULL,
                worldspace_base_id INTEGER NOT NULL,
                cell_mod_id INTEGER NOT NULL,
                cell_base_id INTEGER NOT NULL,
                position_x REAL NOT NULL,
                position_y REAL NOT NULL,
                position_z REAL NOT NULL,
                health REAL NOT NULL,
                magicka REAL NOT NULL,
                stamina REAL NOT NULL,
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL
            );
        )sql");
        Execute("CREATE INDEX IF NOT EXISTS idx_characters_owner_profile_id ON characters (owner_profile_id, id);");
        Execute("UPDATE schema_version SET version = 1 WHERE id = 1;");
    }

    if (schemaVersion < 2)
    {
        // Renewable encounter restart state (world roadmap W08): only the
        // minimum a restart needs, no tick-absolute values.
        Execute(R"sql(
            CREATE TABLE IF NOT EXISTS renewable_encounters (
                cell_form_id INTEGER NOT NULL CHECK (cell_form_id > 0 AND cell_form_id <= 4294967295),
                group_index INTEGER NOT NULL CHECK (group_index >= 0 AND group_index <= 4294967295),
                epoch INTEGER NOT NULL CHECK (epoch >= 0),
                cleared INTEGER NOT NULL CHECK (cleared IN (0, 1)),
                cooldown_remaining_ticks INTEGER NOT NULL CHECK (cooldown_remaining_ticks >= 0),
                updated_at INTEGER NOT NULL,
                PRIMARY KEY (cell_form_id, group_index)
            ) WITHOUT ROWID;
        )sql");
        Execute("UPDATE schema_version SET version = 2 WHERE id = 1;");
    }

    if (schemaVersion < 3)
    {
        Execute("ALTER TABLE characters ADD COLUMN slot_index INTEGER NOT NULL DEFAULT 0;");
        Execute("ALTER TABLE characters ADD COLUMN needs_race_menu INTEGER NOT NULL DEFAULT 0 CHECK (needs_race_menu IN (0, 1));");

        struct ExistingCharacter final
        {
            std::int64_t Id{};
            std::string OwnerProfileId;
        };
        std::vector<ExistingCharacter> existingCharacters;
        {
            auto statement = Prepare("SELECT id, owner_profile_id FROM characters ORDER BY owner_profile_id COLLATE BINARY ASC, id ASC;");
            while (statement.Step())
                existingCharacters.push_back({statement.ColumnInt64(0), statement.ColumnText(1)});
        }

        std::unordered_map<std::string, std::int64_t> nextSlotByOwner;
        auto updateSlot = Prepare("UPDATE characters SET slot_index = ? WHERE id = ?;");
        for (const auto& character : existingCharacters)
        {
            const std::int64_t slotIndex = nextSlotByOwner[character.OwnerProfileId]++;
            updateSlot.Bind(1, slotIndex);
            updateSlot.Bind(2, character.Id);
            (void)updateSlot.Step();
            updateSlot = Prepare("UPDATE characters SET slot_index = ? WHERE id = ?;");
        }

        Execute("CREATE UNIQUE INDEX IF NOT EXISTS idx_characters_owner_slot ON characters (owner_profile_id, slot_index);");
        Execute("UPDATE schema_version SET version = 3 WHERE id = 1;");
    }

    if (schemaVersion < 4)
    {
        Execute(R"sql(
            CREATE TABLE IF NOT EXISTS world_objects (
                object_mod_id INTEGER NOT NULL CHECK (object_mod_id >= 0 AND object_mod_id <= 4294967295),
                object_base_id INTEGER NOT NULL CHECK (object_base_id > 0 AND object_base_id <= 4294967295),
                cell_mod_id INTEGER NOT NULL CHECK (cell_mod_id >= 0 AND cell_mod_id <= 4294967295),
                cell_base_id INTEGER NOT NULL CHECK (cell_base_id > 0 AND cell_base_id <= 4294967295),
                worldspace_mod_id INTEGER NOT NULL CHECK (worldspace_mod_id >= 0 AND worldspace_mod_id <= 4294967295),
                worldspace_base_id INTEGER NOT NULL CHECK (worldspace_base_id >= 0 AND worldspace_base_id <= 4294967295),
                center_x INTEGER NOT NULL,
                center_y INTEGER NOT NULL,
                is_door INTEGER NOT NULL CHECK (is_door IN (0, 1)),
                door_is_open INTEGER NOT NULL CHECK (door_is_open IN (0, 1)),
                activation_count INTEGER NOT NULL CHECK (activation_count >= 0 AND activation_count <= 4294967295),
                is_harvestable INTEGER NOT NULL CHECK (is_harvestable IN (0, 1)),
                is_harvest_item INTEGER NOT NULL CHECK (is_harvest_item IN (0, 1)),
                is_harvested INTEGER NOT NULL CHECK (is_harvested IN (0, 1)),
                harvest_respawn_at_unix INTEGER NOT NULL CHECK (harvest_respawn_at_unix >= 0),
                is_open_loot INTEGER NOT NULL CHECK (is_open_loot IN (0, 1)),
                is_loot_taken INTEGER NOT NULL CHECK (is_loot_taken IN (0, 1)),
                loot_respawn_at_unix INTEGER NOT NULL CHECK (loot_respawn_at_unix >= 0),
                updated_at INTEGER NOT NULL,
                PRIMARY KEY (object_mod_id, object_base_id, cell_mod_id, cell_base_id)
            ) WITHOUT ROWID;
        )sql");
        Execute("UPDATE schema_version SET version = 4 WHERE id = 1;");
    }

    if (schemaVersion < 5)
    {
        // RaceMenu result (appearance buffer + face tints), hex of CharacterLookCodec. NULL until set.
        Execute("ALTER TABLE characters ADD COLUMN look TEXT NULL;");
        Execute("UPDATE schema_version SET version = 5 WHERE id = 1;");
    }

    if (schemaVersion < 6)
    {
        // In-game clock, one row. Time scale stays a server setting.
        Execute(R"sql(
            CREATE TABLE IF NOT EXISTS world_clock (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                time REAL NOT NULL CHECK (time >= 0 AND time < 24),
                day INTEGER NOT NULL CHECK (day >= 0 AND day <= 31),
                month INTEGER NOT NULL CHECK (month >= 0 AND month < 12),
                year INTEGER NOT NULL CHECK (year >= 0 AND year <= 999),
                updated_at INTEGER NOT NULL
            );
        )sql");
        Execute("UPDATE schema_version SET version = 6 WHERE id = 1;");
    }

    transaction.Commit();
    spdlog::info("[Persistence] SQLite database '{}' is ready at schema version {}", m_path.string(), kCurrentSchemaVersion);
}

void Database::Execute(const std::string_view acSql)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    const std::string sql(acSql);
    char* pError = nullptr;
    const int result = sqlite3_exec(m_database.get(), sql.c_str(), nullptr, nullptr, &pError);
    if (result == SQLITE_OK)
        return;

    std::string message = pError ? pError : sqlite3_errmsg(m_database.get());
    if (pError)
        sqlite3_free(pError);

    const std::string operation = "Failed to execute SQLite SQL: " + message;
    spdlog::error("[Persistence] {} (SQLite error {})", operation, result);
    throw std::runtime_error(operation);
}

Database::Statement Database::Prepare(const std::string_view acSql)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    if (acSql.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("SQLite statement is too large");

    sqlite3_stmt* pStatement = nullptr;
    const int result = sqlite3_prepare_v2(m_database.get(), acSql.data(), static_cast<int>(acSql.size()), &pStatement, nullptr);
    if (result != SQLITE_OK)
    {
        if (pStatement)
            sqlite3_finalize(pStatement);
        ThrowSqliteError(m_database.get(), result, "Failed to prepare SQLite statement");
    }

    return Statement(pStatement, std::move(lock));
}

std::int64_t Database::LastInsertRowId() const noexcept
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return sqlite3_last_insert_rowid(m_database.get());
}

int Database::Changes() const noexcept
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return sqlite3_changes(m_database.get());
}

void Database::RollbackNoThrow() noexcept
{
    char* pError = nullptr;
    const int result = sqlite3_exec(m_database.get(), "ROLLBACK TRANSACTION;", nullptr, nullptr, &pError);
    if (result != SQLITE_OK)
    {
        spdlog::error("[Persistence] Failed to roll back SQLite transaction: {}", pError ? pError : sqlite3_errmsg(m_database.get()));
        if (pError)
            sqlite3_free(pError);
    }
}
} // namespace Persistence
