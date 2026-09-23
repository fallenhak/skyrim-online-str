#pragma once

#include <Persistence/Database.h>

#include <cstdint>
#include <vector>

namespace Persistence
{
/**
 * The minimum renewable encounter state that must survive a server restart
 * (roadmap W08): which reset cycle an encounter is in, whether it is cleared,
 * and how much of its reset cooldown is left. Slots, incarnations, occupancy
 * and spawn claims are runtime-only and rebuilt from configuration.
 *
 * Nothing tick-absolute is stored because server ticks start over with the
 * process. It mirrors RenewableEncounterRegistry::EncounterSnapshot without
 * depending on the world services.
 */
struct RenewableEncounterRecord final
{
    std::uint32_t CellFormId{};
    std::uint32_t GroupIndex{};
    std::uint64_t Epoch{};
    bool Cleared{};
    std::uint64_t CooldownRemainingTicks{};
};

struct RenewableEncounterRepository final
{
    explicit RenewableEncounterRepository(Database& aDatabase) noexcept;
    ~RenewableEncounterRepository() noexcept = default;

    RenewableEncounterRepository(const RenewableEncounterRepository&) = delete;
    RenewableEncounterRepository& operator=(const RenewableEncounterRepository&) = delete;
    RenewableEncounterRepository(RenewableEncounterRepository&&) = delete;
    RenewableEncounterRepository& operator=(RenewableEncounterRepository&&) = delete;

    // Replaces the stored snapshot atomically. A malformed snapshot (invalid or
    // duplicate id, a value SQLite cannot hold, a cooldown on a live
    // encounter) is rejected as a whole and the stored one is kept.
    [[nodiscard]] bool SaveAll(const std::vector<RenewableEncounterRecord>& acRecords);
    // Ordered by (cell, group). Rows that fail validation are skipped.
    [[nodiscard]] std::vector<RenewableEncounterRecord> LoadAll() const;

private:
    Database& m_database;
};
} // namespace Persistence
