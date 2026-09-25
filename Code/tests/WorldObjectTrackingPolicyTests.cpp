#include <TiltedCore/Stl.hpp>

#include <Services/WorldObjectTrackingPolicy.h>

#include <catch2/catch.hpp>

TEST_CASE("Offline world item pickups and harvests are not tracked for server restoration", "[world_object_tracking]")
{
    REQUIRE_FALSE(WorldObjectTrackingPolicy::ShouldTrackWorldItem(false, true));
    REQUIRE_FALSE(WorldObjectTrackingPolicy::ShouldTrackWorldItem(true, false));
    REQUIRE(WorldObjectTrackingPolicy::ShouldTrackWorldItem(true, true));

    REQUIRE_FALSE(WorldObjectTrackingPolicy::ShouldTrackHarvest(false, true));
    REQUIRE_FALSE(WorldObjectTrackingPolicy::ShouldTrackHarvest(true, false));
    REQUIRE(WorldObjectTrackingPolicy::ShouldTrackHarvest(true, true));
}
