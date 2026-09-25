#include <TiltedCore/Stl.hpp>

#include <Services/ActivatorReplayPolicy.h>

#include <catch2/catch.hpp>

TEST_CASE("Trap, hazard and pressure plate activator history is never replayed", "[activator_replay]")
{
    REQUIRE(ActivatorReplayPolicy::Classify("TrapTriggerPressurePlate") == ActivatorReplayPolicy::Kind::kNeverReplay);
    REQUIRE(ActivatorReplayPolicy::Classify("DLC1HazardFire") == ActivatorReplayPolicy::Kind::kNeverReplay);
    REQUIRE(ActivatorReplayPolicy::Classify("") == ActivatorReplayPolicy::Kind::kNeverReplay);
    REQUIRE(ActivatorReplayPolicy::ReplayCount(ActivatorReplayPolicy::Kind::kNeverReplay, 100, 0) == 0);
}

TEST_CASE("Two-state lever and button activators replay only the missing parity", "[activator_replay]")
{
    REQUIRE(ActivatorReplayPolicy::Classify("GenPullChain01") == ActivatorReplayPolicy::Kind::kTwoStateToggle);
    REQUIRE(ActivatorReplayPolicy::Classify("PuzzleButton") == ActivatorReplayPolicy::Kind::kTwoStateToggle);
    REQUIRE(ActivatorReplayPolicy::ReplayCount(ActivatorReplayPolicy::Kind::kTwoStateToggle, 7, 2) == 1);
    REQUIRE(ActivatorReplayPolicy::ReplayCount(ActivatorReplayPolicy::Kind::kTwoStateToggle, 8, 2) == 0);
}

TEST_CASE("Other activator history replays at most three activations", "[activator_replay]")
{
    constexpr auto kind = ActivatorReplayPolicy::Kind::kBoundedReplay;
    REQUIRE(ActivatorReplayPolicy::ReplayCount(kind, 1, 0) == 1);
    REQUIRE(ActivatorReplayPolicy::ReplayCount(kind, 2, 0) == 2);
    REQUIRE(ActivatorReplayPolicy::ReplayCount(kind, 100, 0) == ActivatorReplayPolicy::kMaxReplayCount);
    REQUIRE(ActivatorReplayPolicy::ReplayCount(kind, 2, 3) == 0);
}
