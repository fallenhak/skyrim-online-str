#pragma once

#include <Services/LeveledItemResolver.h>

#include <Structs/GameId.h>
#include <Structs/Inventory.h>

#include <optional>
#include <unordered_map>

namespace ESLoader
{
struct RecordCollection;
}
struct ModsComponent;
class EncounterZoneIndex;

// World-state plan, phase 1b: the starting contents of a placed container come from
// its CONT record, with leveled lists resolved on the server at the place's fixed level.
// Every client then receives the same contents, independent of who opened it first.
class PluginContainerContents
{
public:
    // Level for places without an encounter zone (towns, open world). Open decision:
    // #81 / Burak, 2026-09-26. Configurable later.
    static constexpr uint16_t kDefaultPlaceLevel = 10;

    struct Result
    {
        Inventory Contents{};
        uint16_t Level{};
        uint32_t ZoneId{};
    };

    PluginContainerContents(const ESLoader::RecordCollection* apRecords, const EncounterZoneIndex& acZones) noexcept;

    // Empty when the reference is not a plugin-placed container or an item's plugin is not
    // registered by any client yet; the caller then keeps the previous behaviour.
    [[nodiscard]] std::optional<Result> Build(const GameId& acReferenceId, const ModsComponent& acMods) noexcept;

private:
    [[nodiscard]] const LeveledItemResolver::List* FindList(uint32_t aFormId) noexcept;

    const ESLoader::RecordCollection* m_pRecords;
    const EncounterZoneIndex& m_zones;
    std::unordered_map<uint32_t, LeveledItemResolver::List> m_listCache;
};
