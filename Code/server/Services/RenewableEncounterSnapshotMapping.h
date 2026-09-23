#pragma once

#include <Persistence/RenewableEncounterRepository.h>
#include <Services/RenewableEncounterRegistry.h>

#include <cstddef>
#include <set>
#include <vector>

/**
 * Converts between the registry's restart snapshot and the persisted records
 * (roadmap W11, integration of W08). Kept free of the world and the database so
 * the mapping is unit tested standalone.
 */
[[nodiscard]] inline std::vector<Persistence::RenewableEncounterRecord> ToRenewableEncounterRecords(
    const std::vector<RenewableEncounterRegistry::EncounterSnapshot>& acSnapshot)
{
    std::vector<Persistence::RenewableEncounterRecord> records;
    records.reserve(acSnapshot.size());
    for (const auto& entry : acSnapshot)
        records.push_back(Persistence::RenewableEncounterRecord{entry.Id.CellFormId, entry.Id.GroupIndex, entry.Epoch, entry.Cleared, entry.CooldownRemainingTicks});

    return records;
}

struct RestorableEncounterSnapshot final
{
    std::vector<RenewableEncounterRegistry::EncounterSnapshot> Entries;
    // Records naming an encounter the current configuration no longer has.
    std::size_t Skipped{};
};

/**
 * Keeps only records for encounters the registry was configured with.
 * RenewableEncounterRegistry::Restore rejects a snapshot naming an unknown
 * encounter as a whole, so an encounter removed from the configuration would
 * otherwise reset every other encounter's persisted state on the next start.
 */
[[nodiscard]] inline RestorableEncounterSnapshot ToRestorableSnapshot(
    const RenewableEncounterRegistry& acRegistry, const std::vector<Persistence::RenewableEncounterRecord>& acRecords)
{
    std::set<RenewableEncounterId> configured;
    for (const auto& entry : acRegistry.Snapshot(0))
        configured.insert(entry.Id);

    RestorableEncounterSnapshot result;
    for (const auto& record : acRecords)
    {
        const RenewableEncounterId id{record.CellFormId, record.GroupIndex};
        if (!configured.count(id))
        {
            ++result.Skipped;
            continue;
        }

        result.Entries.push_back(RenewableEncounterRegistry::EncounterSnapshot{id, record.Epoch, record.Cleared, record.CooldownRemainingTicks});
    }

    return result;
}
