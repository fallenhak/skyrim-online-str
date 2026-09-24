#include <Services/ProjectileLaunchAuthorityPolicy.h>

#include <catch2/catch.hpp>

#include <limits>

TEST_CASE("Projectile launches require the current owner and incarnation", "[actor_authority]")
{
    REQUIRE(ProjectileLaunchAuthorityPolicy::IsAuthorized(true, true, true, 11));
    REQUIRE_FALSE(ProjectileLaunchAuthorityPolicy::IsAuthorized(false, true, true, 11));
    REQUIRE_FALSE(ProjectileLaunchAuthorityPolicy::IsAuthorized(true, false, true, 11));
    REQUIRE_FALSE(ProjectileLaunchAuthorityPolicy::IsAuthorized(true, true, false, 11));
    REQUIRE_FALSE(ProjectileLaunchAuthorityPolicy::IsAuthorized(true, true, true, 0));
}

TEST_CASE("Projectile launch numeric parameters must be finite", "[actor_authority]")
{
    REQUIRE(ProjectileLaunchAuthorityPolicy::HasFiniteParameters(0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 1.f));
    REQUIRE_FALSE(ProjectileLaunchAuthorityPolicy::HasFiniteParameters(
        std::numeric_limits<float>::quiet_NaN(), 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 1.f));
    REQUIRE_FALSE(ProjectileLaunchAuthorityPolicy::HasFiniteParameters(
        0.f, 1.f, 2.f, 3.f, 4.f, 5.f, std::numeric_limits<float>::infinity(), 1.f));
    REQUIRE_FALSE(ProjectileLaunchAuthorityPolicy::HasFiniteParameters(
        0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, -std::numeric_limits<float>::infinity()));
}
