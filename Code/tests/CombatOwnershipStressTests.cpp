#include <Services/ActorMutationAuthorityPolicy.h>
#include <Services/CombatAttackerAuthorizationPolicy.h>
#include <Services/CombatContributionLedger.h>
#include <Services/PendingCombatObservationStore.h>

#include <catch2/catch.hpp>

#include <cstdint>
#include <vector>

namespace
{
CombatAttackerAuthorizationInput MakeObservationAuthority(
    const std::uint32_t aAttackerServerId,
    const Persistence::CharacterId aCharacterId,
    const std::uint32_t aCurrentOwnershipEpoch,
    const std::uint32_t aRequestedOwnershipEpoch,
    const bool aSessionIsInWorld = true,
    const bool aAttackerExists = true)
{
    CombatAttackerAuthorizationInput input{};
    input.SessionIsInWorld = aSessionIsInWorld;
    if (aSessionIsInWorld)
        input.SessionCharacterId = aCharacterId;
    input.AttackerServerId = aAttackerServerId;
    input.AttackerEntityExists = aAttackerExists;
    input.AttackerIsPlayerCharacter = true;
    input.OwnerExists = aAttackerExists;
    input.SenderIsCurrentOwner = aAttackerExists && aRequestedOwnershipEpoch == aCurrentOwnershipEpoch;
    input.RequestedOwnershipEpoch = aRequestedOwnershipEpoch;
    input.CurrentOwnershipEpoch = aCurrentOwnershipEpoch;
    if (aAttackerExists)
        input.ServerResolvedPersistentCharacterId = aCharacterId;
    return input;
}
} // namespace

TEST_CASE("Target owner disconnect and transfer keep the target lifecycle while rejecting the old epoch", "[combat_authority]")
{
    PendingCombatObservationStore<4> pending;
    CombatContributionLedger ledger;
    const ValidatedHitObservation hit{17, 5, 30, 99, 41, 1};
    REQUIRE(pending.TryAppend(hit));

    // The target's owner changes during combat. The lifecycle remains the
    // same, but only a health update from the new owner and epoch is accepted.
    constexpr std::uint32_t oldTargetOwnerEpoch = 8;
    constexpr std::uint32_t currentTargetOwnerEpoch = 9;
    REQUIRE_FALSE(ActorMutationAuthorityPolicy::IsCurrentOwner(true, true, false, oldTargetOwnerEpoch));
    REQUIRE(ActorMutationAuthorityPolicy::IsCurrentOwner(true, true, true, currentTargetOwnerEpoch));

    const auto correlated = pending.TakeForAcceptedHealthDecrease(
        hit.TargetServerId,
        hit.TargetLifecycleGeneration,
        [](const ValidatedHitObservation&) noexcept { return true; });
    REQUIRE(correlated == hit);
    REQUIRE(ledger.RecordValidatedContribution({hit.TargetServerId, hit.TargetLifecycleGeneration}, 42, hit.ObservedTick));

    // The already validated persistent contributor identity survives later
    // target-owner changes and can be consumed only once at death.
    const auto contributors = ledger.ConsumeCharacterIdsForDeath({hit.TargetServerId, hit.TargetLifecycleGeneration}, 2);
    REQUIRE(contributors == std::vector<Persistence::CharacterId>{42});
    REQUIRE(ledger.ConsumeCharacterIdsForDeath({hit.TargetServerId, hit.TargetLifecycleGeneration}, 2).empty());
}

TEST_CASE("Attacker disconnect drops its queued hit before a current attacker is correlated", "[combat_authority]")
{
    PendingCombatObservationStore<4> pending;
    CombatContributionLedger ledger;
    const ValidatedHitObservation disconnectedHit{17, 5, 30, 99, 41, 1};
    const ValidatedHitObservation currentHit{18, 2, 30, 99, 42, 2};

    // Both were accepted while their respective senders had live authority.
    REQUIRE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(
                MakeObservationAuthority(17, 42, 5, 5)) == 42);
    REQUIRE(pending.TryAppend(disconnectedHit));
    REQUIRE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(
                MakeObservationAuthority(18, 84, 2, 2)) == 84);
    REQUIRE(pending.TryAppend(currentHit));

    const auto correlated = pending.TakeForAcceptedHealthDecrease(
        currentHit.TargetServerId,
        currentHit.TargetLifecycleGeneration,
        [](const ValidatedHitObservation& observation) noexcept {
            if (observation.AttackerServerId == 17)
            {
                // The disconnected player's entity and in-world session have
                // already been removed by server lifecycle handling.
                return CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(
                           MakeObservationAuthority(17, 42, 5, observation.AttackerOwnershipEpoch, false, false))
                    .has_value();
            }

            return CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(
                       MakeObservationAuthority(18, 84, 2, observation.AttackerOwnershipEpoch))
                .has_value();
        });

    REQUIRE(correlated == currentHit);
    REQUIRE(pending.Size() == 0);
    REQUIRE(ledger.RecordValidatedContribution({currentHit.TargetServerId, currentHit.TargetLifecycleGeneration}, 84, currentHit.ObservedTick));
    REQUIRE(ledger.ConsumeCharacterIdsForDeath({currentHit.TargetServerId, currentHit.TargetLifecycleGeneration}, 3) ==
            std::vector<Persistence::CharacterId>{84});
}

TEST_CASE("Attacker disconnect and transfer cannot rewrite a contribution already resolved to CharacterId", "[combat_authority]")
{
    CombatContributionLedger ledger;
    const CombatContributionLedger::Target target{30, 99};

    const auto acceptedBeforeTransfer = MakeObservationAuthority(17, 42, 5, 5);
    const auto contributor = CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(acceptedBeforeTransfer);
    REQUIRE(contributor == 42);
    REQUIRE(ledger.RecordValidatedContribution(target, *contributor, 1));

    const auto disconnectedAfterCorrelation = MakeObservationAuthority(17, 42, 5, 5, false, false);
    REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(disconnectedAfterCorrelation).has_value());

    const auto staleAfterTransfer = MakeObservationAuthority(17, 42, 6, 5);
    REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(staleAfterTransfer).has_value());

    // Transfer/disconnect state is not stored as contribution authority. A
    // later stale packet cannot replace the server-resolved persistent ID.
    const auto contributors = ledger.ConsumeCharacterIdsForDeath(target, 2);
    REQUIRE(contributors == std::vector<Persistence::CharacterId>{42});
}
