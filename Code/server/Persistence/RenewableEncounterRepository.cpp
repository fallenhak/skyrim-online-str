#include <Persistence/RenewableEncounterRepository.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <limits>
#include <set>
#include <utility>

namespace Persistence
{
namespace
{
constexpr std::uint64_t kMaxStoredValue = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

[[nodiscard]] std::int64_t GetUnixTimestamp() noexcept
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

[[nodiscard]] bool IsValid(const RenewableEncounterRecord& acRecord) noexcept
{
    if (acRecord.CellFormId == 0 || acRecord.Epoch > kMaxStoredValue || acRecord.CooldownRemainingTicks > kMaxStoredValue)
        return false;

    return acRecord.Cleared || acRecord.CooldownRemainingTicks == 0;
}
} // namespace

RenewableEncounterRepository::RenewableEncounterRepository(Database& aDatabase) noexcept
    : m_database(aDatabase)
{
}

bool RenewableEncounterRepository::SaveAll(const std::vector<RenewableEncounterRecord>& acRecords)
{
    std::set<std::pair<std::uint32_t, std::uint32_t>> ids;
    for (const auto& record : acRecords)
    {
        if (!IsValid(record) || !ids.emplace(record.CellFormId, record.GroupIndex).second)
        {
            spdlog::warn("[Persistence] Rejected renewable encounter snapshot: invalid or duplicate encounter {:X}/{}", record.CellFormId, record.GroupIndex);
            return false;
        }
    }

    const auto now = GetUnixTimestamp();
    Database::Transaction transaction(m_database);
    m_database.Execute("DELETE FROM renewable_encounters;");

    for (const auto& record : acRecords)
    {
        auto insert = m_database.Prepare(
            "INSERT INTO renewable_encounters (cell_form_id, group_index, epoch, cleared, cooldown_remaining_ticks, updated_at) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6);");
        insert.Bind(1, static_cast<std::int64_t>(record.CellFormId));
        insert.Bind(2, static_cast<std::int64_t>(record.GroupIndex));
        insert.Bind(3, static_cast<std::int64_t>(record.Epoch));
        insert.Bind(4, static_cast<std::int64_t>(record.Cleared ? 1 : 0));
        insert.Bind(5, static_cast<std::int64_t>(record.CooldownRemainingTicks));
        insert.Bind(6, now);
        (void)insert.Step();
    }

    transaction.Commit();
    return true;
}

std::vector<RenewableEncounterRecord> RenewableEncounterRepository::LoadAll() const
{
    std::vector<RenewableEncounterRecord> records;
    auto statement = m_database.Prepare(
        "SELECT cell_form_id, group_index, epoch, cleared, cooldown_remaining_ticks FROM renewable_encounters "
        "ORDER BY cell_form_id, group_index;");

    while (statement.Step())
    {
        const auto cell = statement.ColumnInt64(0);
        const auto group = statement.ColumnInt64(1);
        const auto epoch = statement.ColumnInt64(2);
        const auto cleared = statement.ColumnInt64(3);
        const auto cooldown = statement.ColumnInt64(4);

        constexpr auto kMaxId = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
        if (cell <= 0 || cell > kMaxId || group < 0 || group > kMaxId || epoch < 0 || cooldown < 0 || (cleared != 0 && cleared != 1))
        {
            spdlog::warn("[Persistence] Skipped malformed renewable encounter row {}/{}", cell, group);
            continue;
        }

        RenewableEncounterRecord record{};
        record.CellFormId = static_cast<std::uint32_t>(cell);
        record.GroupIndex = static_cast<std::uint32_t>(group);
        record.Epoch = static_cast<std::uint64_t>(epoch);
        record.Cleared = cleared == 1;
        record.CooldownRemainingTicks = static_cast<std::uint64_t>(cooldown);
        if (!IsValid(record))
        {
            spdlog::warn("[Persistence] Skipped malformed renewable encounter row {}/{}", cell, group);
            continue;
        }

        records.push_back(record);
    }

    return records;
}
} // namespace Persistence
