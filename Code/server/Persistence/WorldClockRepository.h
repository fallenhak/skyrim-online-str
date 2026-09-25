#pragma once

#include <Persistence/Database.h>

#include <cstdint>
#include <optional>

namespace Persistence
{
/**
 * The in-game date and time, so a server restart continues the world clock
 * instead of going back to Gameplay:uStartHour. Time scale is not stored: it
 * stays a server setting.
 */
struct WorldClockRecord final
{
    // Skyrim encoding: hours + minutes * 0.017 (see CalendarService::SetTime).
    float Time{};
    std::uint32_t Day{};
    std::uint32_t Month{};
    std::uint32_t Year{};
};

struct WorldClockRepository final
{
    explicit WorldClockRepository(Database& aDatabase) noexcept;
    ~WorldClockRepository() noexcept = default;

    WorldClockRepository(const WorldClockRepository&) = delete;
    WorldClockRepository& operator=(const WorldClockRepository&) = delete;
    WorldClockRepository(WorldClockRepository&&) = delete;
    WorldClockRepository& operator=(WorldClockRepository&&) = delete;

    // An out-of-range clock is rejected and the stored one is kept.
    [[nodiscard]] bool Save(const WorldClockRecord& acRecord);
    // Empty when nothing was saved yet or the stored row is malformed.
    [[nodiscard]] std::optional<WorldClockRecord> Load() const;

    [[nodiscard]] static bool IsValid(const WorldClockRecord& acRecord) noexcept;

private:
    Database& m_database;
};
} // namespace Persistence
