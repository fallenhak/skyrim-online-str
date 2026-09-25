#include <TiltedCore/Stl.hpp>

#include <Services/CorpseRetentionPolicy.h>

#include <catch2/catch.hpp>

#include <cstdint>
#include <limits>

TEST_CASE("creature corpse lifetime uses real-time seconds and expires at its configured boundary", "[corpse_retention]")
{
    REQUIRE(CorpseRetentionPolicy::kDefaultLifetimeSeconds == 30u * 60u);

    constexpr std::uint64_t deathTick = 125;
    constexpr std::uint32_t lifetimeSeconds = 1800;
    constexpr auto expiresAt = CorpseRetentionPolicy::ExpirationTick(deathTick, lifetimeSeconds);

    REQUIRE(expiresAt == deathTick + lifetimeSeconds);
    REQUIRE(CorpseRetentionPolicy::IsRetained(true, true, expiresAt, deathTick));
    REQUIRE(CorpseRetentionPolicy::IsRetained(true, true, expiresAt, expiresAt - 1));
    REQUIRE_FALSE(CorpseRetentionPolicy::IsRetained(true, true, expiresAt, expiresAt));
    REQUIRE(CorpseRetentionPolicy::IsExpired(true, true, expiresAt, expiresAt));
}

TEST_CASE("corpse retention is independent of nearby players and clamps tick overflow", "[corpse_retention]")
{
    const auto expiresAt = CorpseRetentionPolicy::ExpirationTick(40, 20);
    // Player presence is deliberately not an input to these lifetime rules.
    REQUIRE(CorpseRetentionPolicy::IsRetained(true, true, expiresAt, 59));
    REQUIRE(CorpseRetentionPolicy::IsExpired(true, true, expiresAt, 60));

    REQUIRE(CorpseRetentionPolicy::ExpirationTick(std::numeric_limits<std::uint64_t>::max() - 1, 10) ==
            std::numeric_limits<std::uint64_t>::max());
    REQUIRE(CorpseRetentionPolicy::ExpirationTick(42, 0) == 42);
}

TEST_CASE("accepted corpse incarnations cannot be revived by later client death-state reports", "[corpse_retention]")
{
    REQUIRE_FALSE(CorpseRetentionPolicy::AllowsDeathStateChange(true, true, false));
    REQUIRE(CorpseRetentionPolicy::AllowsDeathStateChange(true, true, true));
    REQUIRE(CorpseRetentionPolicy::AllowsDeathStateChange(false, true, false));
}

TEST_CASE("only a dead incarnation with a server corpse marker expires", "[corpse_retention]")
{
    constexpr std::uint64_t expiresAt = 100;
    REQUIRE_FALSE(CorpseRetentionPolicy::IsExpired(false, true, expiresAt, expiresAt));
    REQUIRE_FALSE(CorpseRetentionPolicy::IsExpired(true, false, expiresAt, expiresAt));
    REQUIRE_FALSE(CorpseRetentionPolicy::IsExpired(true, true, expiresAt, expiresAt - 1));
}

TEST_CASE("respawning a retained dead actor clears its old corpse lifetime", "[corpse_retention]")
{
    REQUIRE(CorpseRetentionPolicy::ShouldClearForRespawn(true, true));
    REQUIRE_FALSE(CorpseRetentionPolicy::ShouldClearForRespawn(false, true));
    REQUIRE_FALSE(CorpseRetentionPolicy::ShouldClearForRespawn(true, false));
}
