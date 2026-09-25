#include <TiltedCore/Stl.hpp>

#include <Services/PuzzlePillarPolicy.h>

#include <catch2/catch.hpp>

TEST_CASE("Puzzle pillars are recognised by base form", "[puzzle_pillar]")
{
    REQUIRE(PuzzlePillarPolicy::IsPuzzlePillar(0x000AA8CE));
    REQUIRE_FALSE(PuzzlePillarPolicy::IsPuzzlePillar(0x0006A9E2));
}

TEST_CASE("Fast local clicks on a pillar are swallowed until it settles", "[puzzle_pillar]")
{
    PuzzlePillarPolicy::Lockout lockout;
    constexpr uint32_t pillar = 0xAA8CB;

    REQUIRE(lockout.TryActivate(pillar, true, 10'000));
    REQUIRE_FALSE(lockout.TryActivate(pillar, true, 10'175)); // Hak's clicks were ~175 ms apart
    REQUIRE_FALSE(lockout.TryActivate(pillar, true, 11'999));
    REQUIRE(lockout.TryActivate(pillar, true, 12'000));

    // Another pillar is independent.
    REQUIRE(lockout.TryActivate(0xAA8CC, true, 12'001));
}

TEST_CASE("Remote pillar activations always apply and restart the lockout", "[puzzle_pillar]")
{
    PuzzlePillarPolicy::Lockout lockout;
    constexpr uint32_t pillar = 0xAA8CB;

    REQUIRE(lockout.TryActivate(pillar, false, 5'000));
    REQUIRE(lockout.TryActivate(pillar, false, 5'100));
    REQUIRE_FALSE(lockout.TryActivate(pillar, true, 6'000));
    REQUIRE(lockout.TryActivate(pillar, true, 7'100));
}
