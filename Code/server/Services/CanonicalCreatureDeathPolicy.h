#pragma once

#include <server/Components.h>

/**
 * @brief Selects accepted canonical transitions that represent a trusted
 * Creature's first alive-to-dead transition in its current lifecycle.
 */
struct CanonicalCreatureDeathPolicy final
{
    [[nodiscard]] static bool IsEligibleTransition(
        const bool aWasDead,
        const CharacterComponent* apCharacter,
        const ActorPopulationIdentityComponent* apPopulationIdentity,
        const ActorLifecycleComponent* apLifecycle) noexcept
    {
        return !aWasDead && apCharacter != nullptr && apCharacter->IsDead() && !apCharacter->IsPlayer() &&
               !apCharacter->IsMount() && !apCharacter->IsPlayerSummon() && apPopulationIdentity != nullptr &&
               apPopulationIdentity->Source != ActorPopulationIdentitySource::kPlayer && apPopulationIdentity->IsTrustedCreature() &&
               apLifecycle != nullptr && apLifecycle->IsValid();
    }

    [[nodiscard]] static bool TryAcceptTransition(
        const bool aWasDead,
        const CharacterComponent* apCharacter,
        const ActorPopulationIdentityComponent* apPopulationIdentity,
        ActorLifecycleComponent* apLifecycle) noexcept
    {
        return IsEligibleTransition(aWasDead, apCharacter, apPopulationIdentity, apLifecycle) &&
               apLifecycle->TryMarkCanonicalCreatureDeathAccepted();
    }
};
