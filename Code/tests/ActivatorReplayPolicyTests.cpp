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

TEST_CASE("Activators are classified by base form when the editor id is missing at runtime", "[activator_replay]")
{
    using ActivatorReplayPolicy::Kind;
    // Bleak Falls Barrow pillars (NorDefaultPuzzlePillar01) and a Nordic lever, as the game reports them: no editor id.
    REQUIRE(ActivatorReplayPolicy::ClassifyBaseForm(0x000AA8CE, "") == Kind::kThreeFaceRotation);
    REQUIRE(ActivatorReplayPolicy::ClassifyBaseForm(0x00021513, "") == Kind::kTwoStateToggle);
    REQUIRE(ActivatorReplayPolicy::ClassifyBaseForm(0x00026858, "") == Kind::kTwoStateToggle);
    // Anything else stays unreplayed without an editor id (quest triggers, shrines, traps).
    REQUIRE(ActivatorReplayPolicy::ClassifyBaseForm(0x00012345, "") == Kind::kNeverReplay);
    // A plugin that keeps editor ids still gets the name-based classification.
    REQUIRE(ActivatorReplayPolicy::ClassifyBaseForm(0x01000800, "MyModLever") == Kind::kTwoStateToggle);
}

TEST_CASE("Three-face pillars replay only the missing turns modulo three", "[activator_replay]")
{
    constexpr auto kind = ActivatorReplayPolicy::Kind::kThreeFaceRotation;
    REQUIRE(ActivatorReplayPolicy::ReplayCount(kind, 2, 0) == 2);
    REQUIRE(ActivatorReplayPolicy::ReplayCount(kind, 3, 0) == 0);
    REQUIRE(ActivatorReplayPolicy::ReplayCount(kind, 7, 0) == 1);
    REQUIRE(ActivatorReplayPolicy::ReplayCount(kind, 7, 5) == 2);
}

TEST_CASE("Replay tables do not overlap", "[activator_replay]")
{
    for (const auto id : ActivatorReplayPolicy::kTwoStateBaseFormIds)
        REQUIRE(std::find(ActivatorReplayPolicy::kThreeFaceBaseFormIds.begin(), ActivatorReplayPolicy::kThreeFaceBaseFormIds.end(), id) ==
                ActivatorReplayPolicy::kThreeFaceBaseFormIds.end());
}
