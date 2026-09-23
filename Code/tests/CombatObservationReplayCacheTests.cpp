#include <Services/CombatObservationReplayCache.h>

#include <catch2/catch.hpp>

TEST_CASE("Combat observation replay identity is scoped to attacker authority and target lifecycle", "[combat_authority]")
{
    CombatObservationReplayCache<8> cache;
    const ValidatedHitObservation original{17, 4, 29, 81, 12, 900};

    REQUIRE(cache.TryRemember(original));
    REQUIRE_FALSE(cache.TryRemember(ValidatedHitObservation{17, 4, 29, 81, 12, 901}));

    // The same sequence value belongs to a different authority incarnation.
    REQUIRE(cache.TryRemember(ValidatedHitObservation{17, 5, 29, 81, 12, 900}));
    REQUIRE(cache.TryRemember(ValidatedHitObservation{18, 4, 29, 81, 12, 900}));

    // A fresh target lifecycle has a different replay scope even when its
    // server entity identifier and the attacker's authority are unchanged.
    REQUIRE(cache.TryRemember(ValidatedHitObservation{17, 4, 29, 82, 12, 900}));
    REQUIRE(cache.TryRemember(ValidatedHitObservation{17, 4, 30, 81, 12, 900}));
    REQUIRE(cache.Size() == 5);
}

TEST_CASE("Combat observation replay cache rejects malformed identity without consuming capacity", "[combat_authority]")
{
    CombatObservationReplayCache<2> cache;

    REQUIRE_FALSE(cache.TryRemember(ValidatedHitObservation{0, 1, 2, 3, 4, 5}));
    REQUIRE_FALSE(cache.TryRemember(ValidatedHitObservation{1, 0, 2, 3, 4, 5}));
    REQUIRE_FALSE(cache.TryRemember(ValidatedHitObservation{1, 1, 0, 3, 4, 5}));
    REQUIRE_FALSE(cache.TryRemember(ValidatedHitObservation{1, 1, 2, 0, 4, 5}));
    REQUIRE_FALSE(cache.TryRemember(ValidatedHitObservation{1, 1, 2, 3, 0, 5}));
    REQUIRE(cache.Size() == 0);

    REQUIRE(cache.TryRemember(ValidatedHitObservation{1, 1, 2, 3, 4, 5}));
    REQUIRE(cache.TryRemember(ValidatedHitObservation{1, 1, 2, 3, 5, 6}));
    REQUIRE_FALSE(cache.TryRemember(ValidatedHitObservation{1, 1, 2, 3, 6, 7}));
    REQUIRE(cache.Size() == 2);
}

TEST_CASE("Combat observation replay cache evicts oldest keys within its fixed bound", "[combat_authority]")
{
    CombatObservationReplayCache<3> cache;
    const auto first = ValidatedHitObservation{1, 2, 3, 4, 5, 6};

    REQUIRE(cache.TryRemember(first));
    REQUIRE(cache.TryRemember(ValidatedHitObservation{1, 2, 3, 4, 6, 7}));
    REQUIRE(cache.TryRemember(ValidatedHitObservation{1, 2, 3, 4, 7, 8}));
    REQUIRE_FALSE(cache.TryRemember(first));

    REQUIRE(cache.TryRemember(ValidatedHitObservation{1, 2, 3, 4, 8, 9}));
    REQUIRE(cache.Size() == CombatObservationReplayCache<3>::kCapacity);
    REQUIRE(cache.TryRemember(first));

    cache.Clear();
    REQUIRE(cache.Size() == 0);
    REQUIRE(cache.TryRemember(first));
}
