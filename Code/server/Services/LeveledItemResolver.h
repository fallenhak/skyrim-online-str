#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

// Server-side leveled item resolution (world-state plan, phase 1b). Written from the
// LVLI record semantics documented on UESP (Skyrim_Mod:Mod_File_Format/LVLI); no
// third-party code. The level is a fixed place level (deleveled world), not a player's.
namespace LeveledItemResolver
{
struct Entry
{
    uint16_t Level{};
    uint32_t FormId{};
    uint16_t Count{};
};

struct List
{
    uint8_t ChanceNone{}; // percent
    uint8_t Flags{};
    std::vector<Entry> Entries{};
};

enum Flags : uint8_t
{
    kCalculateFromAllLevels = 0x01,
    kCalculateForEachItem = 0x02,
    kUseAll = 0x04,
};

struct Item
{
    uint32_t FormId{};
    int32_t Count{};
};

// Returns the list for a form id, or nullptr when the form is an item.
using ListLookup = std::function<const List*(uint32_t)>;

inline constexpr uint32_t kMaxDepth = 16;
inline constexpr size_t kMaxItems = 512;

namespace detail
{
inline void AddItem(std::vector<Item>& aItems, const uint32_t aFormId, const int32_t aCount)
{
    if (aFormId == 0 || aCount <= 0)
        return;
    for (auto& item : aItems)
    {
        if (item.FormId == aFormId)
        {
            item.Count += aCount;
            return;
        }
    }
    if (aItems.size() < kMaxItems)
        aItems.push_back({aFormId, aCount});
}

inline void Resolve(const List& acList, uint16_t aLevel, uint32_t aTimes, const ListLookup& acLookup, std::mt19937& aRandom, uint32_t aDepth, std::vector<Item>& aItems);

// One entry picked from a list: an item is added aCount times, a nested list rolled.
inline void Give(const Entry& acEntry, uint32_t aMultiplier, uint16_t aLevel, const ListLookup& acLookup, std::mt19937& aRandom, uint32_t aDepth, std::vector<Item>& aItems)
{
    const uint32_t count = static_cast<uint32_t>(acEntry.Count == 0 ? 1 : acEntry.Count) * aMultiplier;
    if (const List* pNested = acLookup(acEntry.FormId))
    {
        if (aDepth + 1 < kMaxDepth)
            Resolve(*pNested, aLevel, count, acLookup, aRandom, aDepth + 1, aItems);
        return;
    }
    AddItem(aItems, acEntry.FormId, static_cast<int32_t>(count));
}

inline void Resolve(const List& acList, const uint16_t aLevel, const uint32_t aTimes, const ListLookup& acLookup, std::mt19937& aRandom, const uint32_t aDepth, std::vector<Item>& aItems)
{
    std::vector<const Entry*> eligible;
    uint16_t highest = 0;
    for (const auto& entry : acList.Entries)
    {
        if (entry.Level > aLevel || entry.FormId == 0)
            continue;
        eligible.push_back(&entry);
        highest = std::max(highest, entry.Level);
    }
    if (eligible.empty())
        return;

    // Without "calculate from all levels" only the highest eligible level competes.
    if (!(acList.Flags & kCalculateFromAllLevels) && !(acList.Flags & kUseAll))
        std::erase_if(eligible, [highest](const Entry* apEntry) { return apEntry->Level != highest; });

    // "For each item" rolls every unit of a count separately; otherwise one roll is multiplied.
    const bool eachItem = (acList.Flags & kCalculateForEachItem) != 0;
    const uint32_t rolls = eachItem ? aTimes : 1;
    const uint32_t multiplier = eachItem ? 1 : aTimes;

    std::uniform_int_distribution<int> percent(0, 99);
    for (uint32_t roll = 0; roll < rolls && aItems.size() < kMaxItems; ++roll)
    {
        if (percent(aRandom) < acList.ChanceNone)
            continue;

        if (acList.Flags & kUseAll)
        {
            for (const Entry* pEntry : eligible)
                Give(*pEntry, multiplier, aLevel, acLookup, aRandom, aDepth, aItems);
            continue;
        }

        std::uniform_int_distribution<size_t> pick(0, eligible.size() - 1);
        Give(*eligible[pick(aRandom)], multiplier, aLevel, acLookup, aRandom, aDepth, aItems);
    }
}
} // namespace detail

// Resolves aCount rolls of a leveled list at aLevel. aSeed makes the result reproducible
// (a reference id), so a restart before persistence produces the same contents.
[[nodiscard]] inline std::vector<Item> Resolve(const List& acList, const uint16_t aLevel, const uint32_t aCount, const ListLookup& acLookup, const uint32_t aSeed)
{
    std::mt19937 random(aSeed);
    std::vector<Item> items;
    detail::Resolve(acList, aLevel, aCount == 0 ? 1 : aCount, acLookup, random, 0, items);
    return items;
}
} // namespace LeveledItemResolver
