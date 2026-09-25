#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace Persistence
{
struct Database final
{
    struct DatabaseDeleter
    {
        void operator()(sqlite3* apDatabase) const noexcept;
    };

    struct StatementDeleter
    {
        void operator()(sqlite3_stmt* apStatement) const noexcept;
    };

    class Statement final
    {
    public:
        ~Statement() noexcept;

        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;
        Statement(Statement&&) noexcept;
        Statement& operator=(Statement&&) noexcept;

        void Bind(int aIndex, std::string_view acValue);
        void Bind(int aIndex, std::int64_t aValue);
        void Bind(int aIndex, double aValue);

        [[nodiscard]] bool Step();
        [[nodiscard]] std::int64_t ColumnInt64(int aIndex) const noexcept;
        [[nodiscard]] double ColumnDouble(int aIndex) const noexcept;
        [[nodiscard]] std::string ColumnText(int aIndex) const;

    private:
        friend struct Database;

        explicit Statement(sqlite3_stmt* apStatement, std::unique_lock<std::recursive_mutex>&& aLock) noexcept;

        std::unique_lock<std::recursive_mutex> m_lock;
        std::unique_ptr<sqlite3_stmt, StatementDeleter> m_statement;
    };

    class Transaction final
    {
    public:
        explicit Transaction(Database& aDatabase);
        ~Transaction() noexcept;

        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
        Transaction(Transaction&&) = delete;
        Transaction& operator=(Transaction&&) = delete;

        void Commit();

    private:
        Database& m_database;
        std::unique_lock<std::recursive_mutex> m_lock;
        bool m_committed{};
    };

    explicit Database(const std::filesystem::path& acPath);
    ~Database() noexcept;

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    void Migrate();
    void Execute(std::string_view acSql);

    [[nodiscard]] Statement Prepare(std::string_view acSql);
    [[nodiscard]] std::int64_t LastInsertRowId() const noexcept;
    [[nodiscard]] int Changes() const noexcept;
    [[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return m_path; }

private:
    void RollbackNoThrow() noexcept;

    mutable std::recursive_mutex m_mutex;
    std::unique_ptr<sqlite3, DatabaseDeleter> m_database;
    std::filesystem::path m_path;
};
} // namespace Persistence
