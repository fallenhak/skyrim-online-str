#include <TiltedCore/Stl.hpp>

#include <Services/ReconnectPolicy.h>

#include <catch2/catch.hpp>

TEST_CASE("Reconnect starts only for a player who dropped from the world", "[reconnect]")
{
    ReconnectPolicy policy;
    REQUIRE_FALSE(policy.OnDisconnected(false, true, 0.0));
    REQUIRE_FALSE(policy.OnDisconnected(true, false, 0.0));
    REQUIRE_FALSE(policy.IsResuming());

    REQUIRE(policy.OnDisconnected(true, true, 100.0));
    REQUIRE(policy.IsResuming());
}

TEST_CASE("Reconnect attempts back off to ten seconds", "[reconnect]")
{
    ReconnectPolicy policy;
    REQUIRE(policy.OnDisconnected(true, true, 0.0));

    REQUIRE_FALSE(policy.TakeAttempt(0.5));
    REQUIRE(policy.TakeAttempt(1.0));
    REQUIRE_FALSE(policy.TakeAttempt(1.0));

    // The attempt fails: the next one waits its backoff from the failure.
    REQUIRE(policy.OnDisconnected(false, true, 1.5));
    REQUIRE_FALSE(policy.TakeAttempt(3.0));
    REQUIRE(policy.TakeAttempt(3.5));

    REQUIRE(ReconnectPolicy::DelayFor(0) == 1.0);
    REQUIRE(ReconnectPolicy::DelayFor(1) == 2.0);
    REQUIRE(ReconnectPolicy::DelayFor(3) == 8.0);
    REQUIRE(ReconnectPolicy::DelayFor(4) == 10.0);
    REQUIRE(ReconnectPolicy::DelayFor(30) == 10.0);
}

TEST_CASE("Reconnect waits for the handshake, then ends in the world", "[reconnect]")
{
    ReconnectPolicy policy;
    REQUIRE(policy.OnDisconnected(true, true, 0.0));
    REQUIRE(policy.TakeAttempt(1.0));

    policy.OnConnected(2.0);
    REQUIRE_FALSE(policy.TakeAttempt(30.0));
    // A handshake that never reaches the world is retried.
    REQUIRE(policy.TakeAttempt(2.0 + ReconnectPolicy::kHandshakeTimeoutSeconds));

    policy.OnWorldEntered();
    REQUIRE_FALSE(policy.IsResuming());
    REQUIRE_FALSE(policy.TakeAttempt(1000.0));
}

TEST_CASE("Reconnect gives up after its attempts and resumes on a manual retry", "[reconnect]")
{
    ReconnectPolicy policy;
    REQUIRE(policy.OnDisconnected(true, true, 0.0));

    double now = 0.0;
    std::uint32_t attempts = 0;
    while (attempts < ReconnectPolicy::kMaxAttempts)
    {
        now += ReconnectPolicy::kMaxDelaySeconds;
        if (policy.TakeAttempt(now))
            ++attempts;
    }
    now += ReconnectPolicy::kMaxDelaySeconds;
    REQUIRE_FALSE(policy.TakeAttempt(now));
    REQUIRE(policy.HasGivenUp());
    REQUIRE(policy.IsResuming());

    policy.OnManualRetry(now);
    REQUIRE_FALSE(policy.HasGivenUp());
    REQUIRE(policy.GetAttempts() == 0);
}
