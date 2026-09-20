#include <Persistence/Database.h>

#include <sqlite3.h>

#include <spdlog/spdlog.h>

#include <limits>
#include <stdexcept>
#include <utility>

namespace Persistence
{
namespace
{
constexpr int kCurrentSchemaVersion = 1;

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

Database::Statement::Statement(sqlite3_stmt* apStatement) noexcept
    : m_statement(apStatement)
{
}

Database::Statement::~Statement() noexcept = default;
Database::Statement::Statement(Statement&&) noexcept = default;
Database::Statement& Database::Statement::operator=(Statement&&) noexcept = default;

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
Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

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

    transaction.Commit();
    spdlog::info("[Persistence] SQLite database '{}' is ready at schema version {}", m_path.string(), kCurrentSchemaVersion);
}

void Database::Execute(const std::string_view acSql)
{
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

    return Statement(pStatement);
}

std::int64_t Database::LastInsertRowId() const noexcept
{
    return sqlite3_last_insert_rowid(m_database.get());
}

int Database::Changes() const noexcept
{
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
