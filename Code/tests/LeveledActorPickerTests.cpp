#include <TiltedCore/Stl.hpp>

#include <Services/LeveledActorPicker.h>

#include <catch2/catch.hpp>

#include <map>
#include <set>

namespace
{
using namespace LeveledItemResolver;

constexpr uint32_t kBanditBase = 0x100;     // placed ACHR base, TPLT -> kBanditTemplate
constexpr uint32_t kBanditTemplate = 0x101; // NPC_, TPLT -> kBanditList
constexpr uint32_t kBanditList = 0x200;     // LVLN
constexpr uint32_t kBanditNested = 0x201;   // LVLN
constexpr uint32_t kBandit01 = 0x301;
constexpr uint32_t kBandit06 = 0x306;
constexpr uint32_t kBanditBoss = 0x328;
} // namespace

TEST_CASE("A placed actor's template chain is followed to its leveled list", "[leveled_actors]")
{
    const std::map<uint32_t, uint32_t> templates{{kBanditBase, kBanditTemplate}, {kBanditTemplate, kBanditList}, {kBandit01, 0}};
    const std::set<uint32_t> lists{kBanditList, kBanditNested};
    const auto templateOf = [&](uint32_t aId) -> uint32_t {
        const auto it = templates.find(aId);
        return it == templates.end() ? 0 : it->second;
    };
    const auto isList = [&](uint32_t aId) { return lists.count(aId) != 0; };

    REQUIRE(LeveledActorPicker::FindLeveledTemplate(kBanditBase, templateOf, isList) == kBanditList);
    // A unique NPC without a leveled template is not a leveled actor.
    REQUIRE(LeveledActorPicker::FindLeveledTemplate(kBandit01, templateOf, isList) == 0);

    // A template cycle terminates.
    const std::map<uint32_t, uint32_t> cycle{{1, 2}, {2, 1}};
    const auto cycleOf = [&](uint32_t aId) -> uint32_t { return cycle.at(aId); };
    REQUIRE(LeveledActorPicker::FindLeveledTemplate(1, cycleOf, isList) == 0);
}

TEST_CASE("The pick depends on the place level, not on who spawns it", "[leveled_actors]")
{
    std::map<uint32_t, List> records;
    records[kBanditNested] = {0, 0, {{1, kBandit01, 1}, {6, kBandit06, 1}}};
    records[kBanditList] = {0, 0, {{1, kBanditNested, 1}, {28, kBanditBoss, 1}}};
    const ListLookup lookup = [&](uint32_t aId) -> const List* {
        const auto it = records.find(aId);
        return it == records.end() ? nullptr : &it->second;
    };

    // Level 6 (Bleak Falls Barrow's minimum): the nested list, and in it the level-6 bandit.
    for (uint32_t seed = 0; seed < 20; ++seed)
        REQUIRE(LeveledActorPicker::PickNpc(records[kBanditList], 6, lookup, seed) == kBandit06);

    // Level 30: only the highest eligible level competes, so the boss.
    REQUIRE(LeveledActorPicker::PickNpc(records[kBanditList], 30, lookup, 5) == kBanditBoss);

    // Same seed, same pick.
    records[kBanditNested].Flags = kCalculateFromAllLevels;
    REQUIRE(LeveledActorPicker::PickNpc(records[kBanditNested], 10, lookup, 99) == LeveledActorPicker::PickNpc(records[kBanditNested], 10, lookup, 99));
}
