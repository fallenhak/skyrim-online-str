#include <catch2/catch.hpp>

#include <Services/LocalOnlyActivators.h>

#include <algorithm>

TEST_CASE("The local-only activator list is sorted for binary search", "[activator]")
{
    REQUIRE(std::is_sorted(LocalOnlyActivators::kBaseFormIds.begin(), LocalOnlyActivators::kBaseFormIds.end()));
    REQUIRE(std::adjacent_find(LocalOnlyActivators::kBaseFormIds.begin(), LocalOnlyActivators::kBaseFormIds.end()) ==
            LocalOnlyActivators::kBaseFormIds.end());
}

TEST_CASE("Ore veins and shrines stay local; levers and pull chains are synced", "[activator]")
{
    REQUIRE(LocalOnlyActivators::Contains(0x000A2C46)); // MineOreIron01
    REQUIRE(LocalOnlyActivators::Contains(0x00071854)); // ShrineofArkay
    REQUIRE(LocalOnlyActivators::Contains(0x000D232D)); // DoomstoneWarrior

    REQUIRE_FALSE(LocalOnlyActivators::Contains(0x00026858)); // GenPullChain01
    REQUIRE_FALSE(LocalOnlyActivators::Contains(0x000F13B4)); // defaultPuzzlePullChain01NoFurn
}
