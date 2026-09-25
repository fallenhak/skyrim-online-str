#pragma once

#include <Services/LeveledItemResolver.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

namespace ESLoader
{
struct RecordCollection;
}
class EncounterZoneIndex;

// World-state plan, deleveled world (#81 9.2): the NPC a placed leveled actor becomes is
// picked by the server at the place's fixed level, not by the owner's player level.
// Skyrim places leveled enemies as ACHR -> NPC_ whose TPLT chain ends in an LVLN.
// The client write side (making the owner spawn this pick) is separate; until then the
// pick is only compared with the client's and logged.
class LeveledActorPicker
{
public:
    static constexpr uint32_t kMaxTemplateDepth = 10;

    struct Pick
    {
        uint32_t NpcFormId{};
        uint32_t LeveledListId{};
        uint16_t Level{};
        uint32_t ZoneId{};
    };

    // Follows NPC_ templates from a base NPC to the first LVLN, or 0. aTemplateOf returns
    // an NPC_'s TPLT target (0 when none or not an NPC_); aIsLeveledList tells LVLN ids apart.
    [[nodiscard]] static uint32_t FindLeveledTemplate(
        uint32_t aNpcFormId, const std::function<uint32_t(uint32_t)>& aTemplateOf, const std::function<bool(uint32_t)>& aIsLeveledList) noexcept
    {
        uint32_t current = aNpcFormId;
        for (uint32_t depth = 0; current != 0 && depth < kMaxTemplateDepth; ++depth)
        {
            const uint32_t next = aTemplateOf(current);
            if (next == 0)
                return 0;
            if (aIsLeveledList(next))
                return next;
            current = next;
        }
        return 0;
    }

    // The first NPC a resolved list gives, or 0 (chance none, or nothing at this level).
    [[nodiscard]] static uint32_t PickNpc(
        const LeveledItemResolver::List& acList, uint16_t aLevel, const LeveledItemResolver::ListLookup& acLookup, uint32_t aSeed) noexcept
    {
        const auto items = LeveledItemResolver::Resolve(acList, aLevel, 1, acLookup, aSeed);
        return items.empty() ? 0 : items.front().FormId;
    }

    LeveledActorPicker(const ESLoader::RecordCollection* apRecords, const EncounterZoneIndex& acZones) noexcept;

    // Empty when the reference is not a plugin-placed leveled actor.
    [[nodiscard]] std::optional<Pick> PickForReference(uint32_t aReferenceFormId) noexcept;

private:
    [[nodiscard]] const LeveledItemResolver::List* FindList(uint32_t aFormId) noexcept;

    const ESLoader::RecordCollection* m_pRecords;
    const EncounterZoneIndex& m_zones;
    std::unordered_map<uint32_t, LeveledItemResolver::List> m_listCache;
};
