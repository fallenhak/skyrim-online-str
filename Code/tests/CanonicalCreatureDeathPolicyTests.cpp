#include <Components.h>
#include <Services/CanonicalCreatureDeathPolicy.h>

#include <catch2/catch.hpp>

namespace
{
ActorPopulationIdentityComponent MakeTrustedCreature()
{
    ActorPopulationIdentityComponent identity;
    identity.Source = ActorPopulationIdentitySource::kServerNpcBase;
    identity.Classification = ActorPopulationClass::kCreature;
    return identity;
}
}

TEST_CASE("canonical Creature death requires a new alive-to-dead transition and current lifecycle", "[combat_authority]")
{
    const auto identity = MakeTrustedCreature();
    const ActorLifecycleComponent lifecycle{17};

    REQUIRE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, true, &identity, &lifecycle));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(true, true, &identity, &lifecycle));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, false, &identity, &lifecycle));
}

TEST_CASE("canonical Creature death rejects missing or untrusted target identity", "[combat_authority]")
{
    const auto identity = MakeTrustedCreature();
    const ActorLifecycleComponent lifecycle{18};
    const ActorLifecycleComponent invalidLifecycle{ActorLifecycleComponent::kInvalidGeneration};
    const ActorPopulationIdentityComponent unknownIdentity;
    auto playerIdentity = MakeTrustedCreature();
    playerIdentity.Source = ActorPopulationIdentitySource::kPlayer;
    auto clientClaimedIdentity = MakeTrustedCreature();
    clientClaimedIdentity.Source = ActorPopulationIdentitySource::kClientClaimedTemporaryBase;

    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, true, nullptr, &lifecycle));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, true, &identity, nullptr));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, true, &identity, &invalidLifecycle));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, true, &unknownIdentity, &lifecycle));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, true, &playerIdentity, &lifecycle));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, true, &clientClaimedIdentity, &lifecycle));
}
