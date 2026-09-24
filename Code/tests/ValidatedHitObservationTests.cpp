#include <Services/ValidatedHitObservation.h>

#include <catch2/catch.hpp>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

TEST_CASE("validated hit observations contain only immutable server entity identity", "[combat_authority]")
{
    const ValidatedHitObservation observation{17, 4, 29, 81, 12, 900, 70};

    REQUIRE(observation.IsWellFormed());
    REQUIRE(observation.AttackerServerId == 17);
    REQUIRE(observation.AttackerOwnershipEpoch == 4);
    REQUIRE(observation.TargetServerId == 29);
    REQUIRE(observation.TargetLifecycleGeneration == 81);
    REQUIRE(observation.ObservationId == 12);
    REQUIRE(observation.ObservedTick == 900);
    REQUIRE(observation.AttackerLifecycleGeneration == 70);

    static_assert(!std::is_assignable_v<decltype(observation.AttackerServerId)&, std::uint32_t>);
    static_assert(!std::is_assignable_v<decltype(observation.TargetLifecycleGeneration)&, std::uint64_t>);
}

TEST_CASE("validated hit observations reject missing identity components", "[combat_authority]")
{
    constexpr auto invalidServerId = std::numeric_limits<ValidatedHitObservation::ServerId>::max();
    REQUIRE_FALSE(ValidatedHitObservation{invalidServerId, 1, 2, 3, 4, 5, 6}.IsWellFormed());
    REQUIRE_FALSE(ValidatedHitObservation{1, 0, 2, 3, 4, 5, 6}.IsWellFormed());
    REQUIRE_FALSE(ValidatedHitObservation{1, 1, invalidServerId, 3, 4, 5, 6}.IsWellFormed());
    REQUIRE_FALSE(ValidatedHitObservation{1, 1, 2, 0, 4, 5, 6}.IsWellFormed());
    REQUIRE_FALSE(ValidatedHitObservation{1, 1, 2, 3, 0, 5, 6}.IsWellFormed());
    REQUIRE_FALSE(ValidatedHitObservation{1, 1, 2, 3, 4, 5, 0}.IsWellFormed());
    REQUIRE_FALSE(ValidatedHitObservation{1, 1, 1, 3, 4, 5, 6}.IsWellFormed());
    REQUIRE_FALSE(ValidatedHitObservation{1, 1, 2, 3, 4, 0, 6}.IsWellFormed());
}

TEST_CASE("validated hit observations allow raw EnTT server ID zero", "[combat_authority]")
{
    REQUIRE(ValidatedHitObservation{0, 1, 2, 3, 4, 5, 6}.IsWellFormed());
    REQUIRE(ValidatedHitObservation{1, 1, 0, 3, 4, 5, 6}.IsWellFormed());
}

TEST_CASE("validated hit observations can be appended without overwriting prior records", "[combat_authority]")
{
    std::vector<ValidatedHitObservation> observations;
    observations.emplace_back(1, 2, 3, 4, 5, 6, 8);
    observations.emplace_back(1, 2, 3, 4, 6, 7, 8);

    REQUIRE(observations.size() == 2);
    REQUIRE(observations[0].ObservationId == 5);
    REQUIRE(observations[1].ObservationId == 6);
    REQUIRE(observations[0].ObservedTick == 6);
    REQUIRE(observations[1].ObservedTick == 7);
}
