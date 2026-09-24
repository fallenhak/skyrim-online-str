#include <Services/AddTargetAuthorityPolicy.h>

#include <catch2/catch.hpp>

TEST_CASE("AddTarget accepts effects reported by the current caster owner", "[actor_authority]")
{
    REQUIRE(AddTargetAuthorityPolicy::IsAuthorized(true, true, false, 7, 7, true, true, true, true, 11, 11));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, false, 7, 7, true, true, true, false, 11, 11));
}

TEST_CASE("AddTarget preserves target-owner and caster-less effect reports", "[actor_authority]")
{
    REQUIRE(AddTargetAuthorityPolicy::IsAuthorized(true, true, true, 7, 7, false, false, false, false, 0, 0));
    REQUIRE(AddTargetAuthorityPolicy::IsAuthorized(true, true, true, 7, 7, true, true, true, false, 11, 11));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, false, 7, 7, false, false, false, false, 0, 0));
}

TEST_CASE("AddTarget requires owned character endpoints and current incarnations", "[actor_authority]")
{
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(false, true, true, 7, 7, false, false, false, false, 0, 0));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(false, false, false, 7, 7, true, true, true, true, 11, 11));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, false, true, 7, 7, false, false, false, false, 0, 0));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, false, 7, 7, true, false, false, false, 11, 11));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, true, 7, 7, true, true, false, false, 11, 11));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, true, 7, 6, false, false, false, false, 0, 0));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, true, 0, 0, false, false, false, false, 0, 0));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, false, 7, 7, true, true, true, true, 0, 0));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, false, 7, 7, true, true, true, true, 10, 11));
    REQUIRE_FALSE(AddTargetAuthorityPolicy::IsAuthorized(true, true, true, 7, 7, false, false, false, false, 1, 0));
}
