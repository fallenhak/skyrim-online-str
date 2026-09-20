#include <Services/ActorValueMutationPolicy.h>

#include <catch2/catch.hpp>

#include <limits>

TEST_CASE("Actor value mutation policy rejects malformed keys and values", "[actor_authority]")
{
    constexpr std::uint32_t kActorValueCount = ActorValueMutationPolicy::kActorValueCount;

    REQUIRE(ActorValueMutationPolicy::IsValidIndexAndValue(0, 0.f, kActorValueCount));
    REQUIRE(ActorValueMutationPolicy::IsValidIndexAndValue(kActorValueCount - 1, -10.f, kActorValueCount));
    REQUIRE_FALSE(ActorValueMutationPolicy::IsValidIndexAndValue(kActorValueCount, 1.f, kActorValueCount));
    REQUIRE_FALSE(ActorValueMutationPolicy::IsValidIndexAndValue(0, std::numeric_limits<float>::quiet_NaN(), kActorValueCount));
    REQUIRE_FALSE(ActorValueMutationPolicy::IsValidIndexAndValue(0, std::numeric_limits<float>::infinity(), kActorValueCount));
    REQUIRE_FALSE(ActorValueMutationPolicy::IsValidIndexAndValue(0, -std::numeric_limits<float>::infinity(), kActorValueCount));
}
