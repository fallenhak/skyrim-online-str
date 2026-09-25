#include <TiltedCore/Stl.hpp>

#include <Services/LeveledItemResolver.h>

#include <catch2/catch.hpp>

#include <map>

namespace
{
using namespace LeveledItemResolver;

constexpr uint32_t kGold = 0xF;
constexpr uint32_t kIronSword = 0x12EB7;
constexpr uint32_t kSteelSword = 0x13989;
constexpr uint32_t kEbonySword = 0x139B1;
constexpr uint32_t kPotion = 0x39BE5;

struct Lists
{
    std::map<uint32_t, List> Records;

    ListLookup Lookup() const
    {
        return [this](uint32_t aFormId) -> const List* {
            const auto it = Records.find(aFormId);
            return it == Records.end() ? nullptr : &it->second;
        };
    }
};

int32_t CountOf(const std::vector<Item>& acItems, uint32_t aFormId)
{
    for (const auto& item : acItems)
        if (item.FormId == aFormId)
            return item.Count;
    return 0;
}
} // namespace

TEST_CASE("Only entries at or below the place level are eligible", "[leveled_items]")
{
    Lists lists;
    // Highest-level-only (no flags): at level 10 only the level-6 steel sword competes.
    lists.Records[1] = {0, 0, {{1, kIronSword, 1}, {6, kSteelSword, 1}, {46, kEbonySword, 1}}};

    for (uint32_t seed = 0; seed < 50; ++seed)
    {
        const auto items = Resolve(lists.Records[1], 10, 1, lists.Lookup(), seed);
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].FormId == kSteelSword);
    }

    // Below every entry level: nothing.
    lists.Records[2] = {0, 0, {{20, kSteelSword, 1}}};
    REQUIRE(Resolve(lists.Records[2], 5, 1, lists.Lookup(), 1).empty());
}

TEST_CASE("Calculate from all levels picks among every eligible entry", "[leveled_items]")
{
    Lists lists;
    lists.Records[1] = {0, kCalculateFromAllLevels, {{1, kIronSword, 1}, {6, kSteelSword, 1}, {46, kEbonySword, 1}}};

    bool sawIron = false, sawSteel = false;
    for (uint32_t seed = 0; seed < 200; ++seed)
    {
        const auto items = Resolve(lists.Records[1], 10, 1, lists.Lookup(), seed);
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].FormId != kEbonySword);
        sawIron |= items[0].FormId == kIronSword;
        sawSteel |= items[0].FormId == kSteelSword;
    }
    REQUIRE(sawIron);
    REQUIRE(sawSteel);
}

TEST_CASE("Use all gives every eligible entry and nested lists are rolled", "[leveled_items]")
{
    Lists lists;
    lists.Records[10] = {0, 0, {{1, kPotion, 2}}};
    lists.Records[1] = {0, kUseAll, {{1, kGold, 25}, {1, 10, 1}, {50, kEbonySword, 1}}};

    const auto items = Resolve(lists.Records[1], 12, 1, lists.Lookup(), 7);
    REQUIRE(CountOf(items, kGold) == 25);
    REQUIRE(CountOf(items, kPotion) == 2);
    REQUIRE(CountOf(items, kEbonySword) == 0);
}

TEST_CASE("Chance none and count multiplication", "[leveled_items]")
{
    Lists lists;
    lists.Records[1] = {100, 0, {{1, kGold, 5}}};
    REQUIRE(Resolve(lists.Records[1], 10, 3, lists.Lookup(), 1).empty());

    // Without "for each item" one roll is multiplied by the container count.
    lists.Records[2] = {0, 0, {{1, kGold, 5}}};
    REQUIRE(CountOf(Resolve(lists.Records[2], 10, 3, lists.Lookup(), 1), kGold) == 15);

    // With it, each unit rolls: a 50% chance none list over many units gives some, not all.
    lists.Records[3] = {50, kCalculateForEachItem, {{1, kGold, 1}}};
    const auto gold = CountOf(Resolve(lists.Records[3], 10, 100, lists.Lookup(), 3), kGold);
    REQUIRE(gold > 20);
    REQUIRE(gold < 80);
}

TEST_CASE("Resolution is reproducible per seed and a self-referencing list terminates", "[leveled_items]")
{
    Lists lists;
    lists.Records[1] = {0, kCalculateFromAllLevels, {{1, kIronSword, 1}, {1, kSteelSword, 1}, {1, kGold, 10}}};
    REQUIRE(Resolve(lists.Records[1], 10, 4, lists.Lookup(), 42).size() == Resolve(lists.Records[1], 10, 4, lists.Lookup(), 42).size());
    const auto a = Resolve(lists.Records[1], 10, 4, lists.Lookup(), 42);
    const auto b = Resolve(lists.Records[1], 10, 4, lists.Lookup(), 42);
    for (size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE(a[i].FormId == b[i].FormId);
        REQUIRE(a[i].Count == b[i].Count);
    }

    lists.Records[5] = {0, kUseAll, {{1, 5, 1}, {1, kGold, 1}}};
    const auto items = Resolve(lists.Records[5], 10, 1, lists.Lookup(), 1);
    REQUIRE(CountOf(items, kGold) == static_cast<int32_t>(kMaxDepth));
}
