#pragma once

#include <Components.h>

/**
 * @brief Selects accepted canonical transitions that represent a trusted
 * Creature's first alive-to-dead transition in its current lifecycle.
 */
struct CanonicalCreatureDeathPolicy final
{
    [[nodiscard]] static bool IsEligibleTransition(
        const bool aWasDead,
        const bool aIsDead,
        const ActorPopulationIdentityComponent* apPopulationIdentity,
        const ActorLifecycleComponent* apLifecycle) noexcept
    {
        return !aWasDead && aIsDead && apPopulationIdentity != nullptr &&
               apPopulationIdentity->Source != ActorPopulationIdentitySource::kPlayer && apPopulationIdentity->IsTrustedCreature() &&
               apLifecycle != nullptr && apLifecycle->IsValid();
    }
};
