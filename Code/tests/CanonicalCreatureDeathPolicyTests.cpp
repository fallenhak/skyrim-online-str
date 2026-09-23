#include <server/Components.h>
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
    CharacterComponent character;
    character.SetDead(true);

    REQUIRE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, &character, &identity, &lifecycle));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(true, &character, &identity, &lifecycle));

    character.SetDead(false);
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, &character, &identity, &lifecycle));
}

TEST_CASE("canonical Creature death rejects missing or untrusted target identity", "[combat_authority]")
{
    const auto identity = MakeTrustedCreature();
    const ActorLifecycleComponent lifecycle{18};
    const ActorLifecycleComponent invalidLifecycle{ActorLifecycleComponent::kInvalidGeneration};
    const ActorPopulationIdentityComponent unknownIdentity;
    CharacterComponent character;
    character.SetDead(true);
    auto playerIdentity = MakeTrustedCreature();
    playerIdentity.Source = ActorPopulationIdentitySource::kPlayer;
    auto clientClaimedIdentity = MakeTrustedCreature();
    clientClaimedIdentity.Source = ActorPopulationIdentitySource::kClientClaimedTemporaryBase;

    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, &character, nullptr, &lifecycle));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, &character, &identity, nullptr));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, &character, &identity, &invalidLifecycle));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, &character, &unknownIdentity, &lifecycle));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, &character, &playerIdentity, &lifecycle));
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, &character, &clientClaimedIdentity, &lifecycle));
}

TEST_CASE("canonical Creature death excludes players, mounts, and player summons", "[combat_authority]")
{
    const auto identity = MakeTrustedCreature();
    const ActorLifecycleComponent lifecycle{19};
    CharacterComponent character;
    character.SetDead(true);

    character.SetPlayer(true);
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, &character, &identity, &lifecycle));

    character.SetPlayer(false);
    character.SetMount(true);
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, &character, &identity, &lifecycle));

    character.SetMount(false);
    character.SetPlayerSummon(true);
    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, &character, &identity, &lifecycle));

    REQUIRE_FALSE(CanonicalCreatureDeathPolicy::IsEligibleTransition(false, nullptr, &identity, &lifecycle));
}
