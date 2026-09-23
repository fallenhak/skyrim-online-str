#include <Services/AddTargetAuthorityPolicy.h>

#include <catch2/catch.hpp>

TEST_CASE("AddTarget accepts effects reported by the current caster owner", "[actor_authority]")
{
    REQUIRE(AddTargetAuthorityPolicy::IsAuthorized(true, true, false, true, true, true, true));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, false, true, true, true, false));
}

TEST_CASE("AddTarget preserves target-owner and caster-less effect reports", "[actor_authority]")
{
    REQUIRE(AddTargetAuthorityPolicy::IsAuthorized(true, true, true, false, false, false, false));
    REQUIRE(AddTargetAuthorityPolicy::IsAuthorized(true, true, true, true, true, true, false));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, false, false, false, false, false));
}

TEST_CASE("AddTarget requires canonical owned character endpoints", "[actor_authority]")
{
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(false, true, true, false, false, false, false));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(false, false, false, true, true, true, true));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, false, true, false, false, false, false));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, false, true, false, false, false));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, true, true, false, true, false));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, true, true, true, false, false));
}
