#include <Persistence/WorldClockRepository.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <cmath>

namespace Persistence
{
namespace
{
// Same bounds CalendarService::SetDate accepts.
constexpr std::uint32_t kMonthsPerYear = 12;
constexpr std::uint32_t kMaxDay = 31;
constexpr std::uint32_t kMaxYear = 999;

[[nodiscard]] std::int64_t GetUnixTimestamp() noexcept
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

WorldClockRepository::WorldClockRepository(Database& aDatabase) noexcept
    : m_database(aDatabase)
{
}

bool WorldClockRepository::IsValid(const WorldClockRecord& acRecord) noexcept
{
    return std::isfinite(acRecord.Time) && acRecord.Time >= 0.f && acRecord.Time < 24.f && acRecord.Month < kMonthsPerYear && acRecord.Day <= kMaxDay &&
           acRecord.Year <= kMaxYear;
}

bool WorldClockRepository::Save(const WorldClockRecord& acRecord)
{
    if (!IsValid(acRecord))
    {
        spdlog::warn("[Persistence] Rejected world clock time={} date={}/{}/{}", acRecord.Time, acRecord.Day, acRecord.Month, acRecord.Year);
        return false;
    }

    auto upsert = m_database.Prepare(
        "INSERT INTO world_clock (id, time, day, month, year, updated_at) VALUES (1, ?1, ?2, ?3, ?4, ?5) "
        "ON CONFLICT (id) DO UPDATE SET time = excluded.time, day = excluded.day, month = excluded.month, year = excluded.year, updated_at = excluded.updated_at;");
    upsert.Bind(1, static_cast<double>(acRecord.Time));
    upsert.Bind(2, static_cast<std::int64_t>(acRecord.Day));
    upsert.Bind(3, static_cast<std::int64_t>(acRecord.Month));
    upsert.Bind(4, static_cast<std::int64_t>(acRecord.Year));
    upsert.Bind(5, GetUnixTimestamp());
    (void)upsert.Step();
    return true;
}

std::optional<WorldClockRecord> WorldClockRepository::Load() const
{
    auto statement = m_database.Prepare("SELECT time, day, month, year FROM world_clock WHERE id = 1;");
    if (!statement.Step())
        return std::nullopt;

    const auto time = statement.ColumnDouble(0);
    const auto day = statement.ColumnInt64(1);
    const auto month = statement.ColumnInt64(2);
    const auto year = statement.ColumnInt64(3);
    if (day < 0 || month < 0 || year < 0 || day > kMaxDay || year > kMaxYear)
    {
        spdlog::warn("[Persistence] Skipped malformed world clock row");
        return std::nullopt;
    }

    WorldClockRecord record{};
    record.Time = static_cast<float>(time);
    record.Day = static_cast<std::uint32_t>(day);
    record.Month = static_cast<std::uint32_t>(month);
    record.Year = static_cast<std::uint32_t>(year);
    if (!IsValid(record))
    {
        spdlog::warn("[Persistence] Skipped malformed world clock row");
        return std::nullopt;
    }

    return record;
}
} // namespace Persistence
