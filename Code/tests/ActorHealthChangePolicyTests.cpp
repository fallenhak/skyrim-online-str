#include <Services/ActorHealthChangePolicy.h>

#include <catch2/catch.hpp>

#include <limits>

TEST_CASE("Health authority requires the current owned entity incarnation", "[actor_authority]")
{
    REQUIRE(ActorHealthChangePolicy::IsAuthorized(true, true, true, 7));
    REQUIRE_FALSE(ActorHealthChangePolicy::IsAuthorized(false, true, true, 7));
    REQUIRE_FALSE(ActorHealthChangePolicy::IsAuthorized(true, false, true, 7));
    REQUIRE_FALSE(ActorHealthChangePolicy::IsAuthorized(true, true, false, 7));
    REQUIRE_FALSE(ActorHealthChangePolicy::IsAuthorized(true, true, true, 0));
}

TEST_CASE("Signed finite health deltas update existing health without insertion", "[actor_authority]")
{
    TiltedPhoques::Map<uint32_t, float> values;
    values.emplace(ActorHealthChangePolicy::kHealthActorValue, 100.f);

    REQUIRE(ActorHealthChangePolicy::TryApplySignedDelta(values, -25.f));
    REQUIRE(values.at(ActorHealthChangePolicy::kHealthActorValue) == 75.f);
    REQUIRE(ActorHealthChangePolicy::TryApplySignedDelta(values, 10.f));
    REQUIRE(values.at(ActorHealthChangePolicy::kHealthActorValue) == 85.f);

    REQUIRE_FALSE(ActorHealthChangePolicy::TryApplySignedDelta(values, std::numeric_limits<float>::quiet_NaN()));
    REQUIRE_FALSE(ActorHealthChangePolicy::TryApplySignedDelta(values, std::numeric_limits<float>::infinity()));
    REQUIRE_FALSE(ActorHealthChangePolicy::TryApplySignedDelta(values, -std::numeric_limits<float>::infinity()));
    REQUIRE(values.at(ActorHealthChangePolicy::kHealthActorValue) == 85.f);

    TiltedPhoques::Map<uint32_t, float> missingHealth;
    REQUIRE_FALSE(ActorHealthChangePolicy::TryApplySignedDelta(missingHealth, -1.f));
    REQUIRE(missingHealth.empty());

    TiltedPhoques::Map<uint32_t, float> overflow;
    overflow.emplace(ActorHealthChangePolicy::kHealthActorValue, std::numeric_limits<float>::max());
    REQUIRE_FALSE(ActorHealthChangePolicy::TryApplySignedDelta(overflow, std::numeric_limits<float>::max()));
    REQUIRE(overflow.at(ActorHealthChangePolicy::kHealthActorValue) == std::numeric_limits<float>::max());
}

TEST_CASE("Canonical health decrease requires the accepted value to be lower", "[actor_authority][combat_authority]")
{
    REQUIRE(ActorHealthChangePolicy::IsCanonicalDecrease(100.f, 75.f));
    REQUIRE_FALSE(ActorHealthChangePolicy::IsCanonicalDecrease(75.f, 85.f));
    REQUIRE_FALSE(ActorHealthChangePolicy::IsCanonicalDecrease(75.f, 75.f));
    REQUIRE_FALSE(ActorHealthChangePolicy::IsCanonicalDecrease(std::numeric_limits<float>::quiet_NaN(), 0.f));

    // A negative submitted delta can round away; it is not evidence that the
    // canonical health value decreased.
    const float previousHealth = 1.0e20f;
    const float roundedHealth = previousHealth - 1.f;
    REQUIRE(roundedHealth == previousHealth);
    REQUIRE_FALSE(ActorHealthChangePolicy::IsCanonicalDecrease(previousHealth, roundedHealth));
}
