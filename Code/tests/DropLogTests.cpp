#include <Services/DropLogThrottle.h>

#include <catch2/catch.hpp>

TEST_CASE("The first drop of a reason is logged at once", "[drop_log]")
{
    DropLogThrottle throttle;
    REQUIRE(throttle.Admit("activate: object not registered", 0) == 1);
}

TEST_CASE("A flood of one reason is one line per window that counts the suppressed drops", "[drop_log]")
{
    DropLogThrottle throttle;
    REQUIRE(throttle.Admit("activate: object not registered", 1000) == 1);

    for (int64_t ms = 1001; ms < 2000; ms += 100)
        REQUIRE(throttle.Admit("activate: object not registered", ms) == 0);

    // 10 suppressed inside the window plus this one.
    REQUIRE(throttle.Admit("activate: object not registered", 2000) == 11);
}

TEST_CASE("Different reasons are throttled separately", "[drop_log]")
{
    DropLogThrottle throttle;
    REQUIRE(throttle.Admit("activate: object not registered", 0) == 1);
    REQUIRE(throttle.Admit("lock change: out of range", 1) == 1);
    REQUIRE(throttle.Admit("activate: object not registered", 2) == 0);
}
