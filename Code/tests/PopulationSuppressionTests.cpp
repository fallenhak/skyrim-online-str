#include <Services/PopulationSuppressionPolicy.h>
#include <Services/PopulationDisableTracker.h>

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

TEST_CASE("Population physical suppression is server-reason and ownership scoped", "[population]")
{
    constexpr auto cHumanoidDenied = CharacterAssignmentRejectReason::kPopulationHumanoidDenied;
    constexpr auto cUnknownDenied = CharacterAssignmentRejectReason::kPopulationUnknownDenied;

    REQUIRE(PopulationSuppressionPolicy::ShouldPhysicallySuppress(cHumanoidDenied));
    REQUIRE_FALSE(PopulationSuppressionPolicy::ShouldPhysicallySuppress(cUnknownDenied));

    REQUIRE(PopulationSuppressionPolicy::ShouldOwnDisable(cHumanoidDenied, 0x123456, false, false, false, false));
    REQUIRE_FALSE(PopulationSuppressionPolicy::ShouldOwnDisable(cUnknownDenied, 0x123456, false, false, false, false));
    REQUIRE_FALSE(PopulationSuppressionPolicy::ShouldOwnDisable(cHumanoidDenied, 0x14, false, false, false, false));
    REQUIRE_FALSE(PopulationSuppressionPolicy::ShouldOwnDisable(cHumanoidDenied, 0x123456, true, false, false, false));
    REQUIRE_FALSE(PopulationSuppressionPolicy::ShouldOwnDisable(cHumanoidDenied, 0xFF000123, false, true, false, false));
    REQUIRE_FALSE(PopulationSuppressionPolicy::ShouldOwnDisable(cHumanoidDenied, 0x123456, false, false, true, false));
    REQUIRE_FALSE(PopulationSuppressionPolicy::ShouldOwnDisable(cHumanoidDenied, 0x123456, false, false, false, true));

    PopulationDisableTracker tracker;
    REQUIRE(tracker.OwnDisable(0x123456));
    REQUIRE_FALSE(tracker.OwnDisable(0x123456));
    REQUIRE_FALSE(tracker.OwnDisable(0x14));
    REQUIRE(tracker.OwnsDisable(0x123456));
    REQUIRE(tracker.Size() == 1);

    // ECS suppression cleanup does not affect this independent session registry.
    REQUIRE(tracker.OwnsDisable(0x123456));

    const auto drained = tracker.DrainOwnedDisables();
    REQUIRE(drained.count(0x123456) == 1);
    REQUIRE(tracker.Size() == 0);
    REQUIRE_FALSE(tracker.OwnsDisable(0x123456));

    // A reference returning after the registry is drained is eligible for a new session.
    REQUIRE(tracker.OwnDisable(0x123456));
}
