#include <Services/PopulationSuppressionPolicy.h>

#include <catch2/catch.hpp>

TEST_CASE("Population rejection suppresses live actors and discards cancelled assignments", "[population]")
{
    REQUIRE(
        PopulationSuppressionPolicy::GetRejectionAction(false) == PopulationAssignmentRejectionAction::kSuppressLiveActor);
    REQUIRE(
        PopulationSuppressionPolicy::GetRejectionAction(true) == PopulationAssignmentRejectionAction::kDestroyCancelledEntity);
    REQUIRE(PopulationSuppressionPolicy::ShouldSkipAssignment(true));
    REQUIRE_FALSE(PopulationSuppressionPolicy::ShouldSkipAssignment(false));
}
