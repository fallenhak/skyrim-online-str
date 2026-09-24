#include <Persistence/PersistenceService.h>

#include <utility>

namespace
{
[[nodiscard]] std::filesystem::path ResolveDatabasePath(std::filesystem::path aDatabasePath)
{
    if (aDatabasePath.empty())
        return PersistenceService::DefaultDatabasePath();

    return aDatabasePath;
}
} // namespace

PersistenceService::PersistenceService(std::filesystem::path aDatabasePath)
    : m_database(ResolveDatabasePath(std::move(aDatabasePath)))
    , m_characterRepository(m_database)
{
    m_database.Migrate();
}

std::filesystem::path PersistenceService::DefaultDatabasePath()
{
    return std::filesystem::current_path() / "Data" / "SkyrimTogetherServer.db";
}
