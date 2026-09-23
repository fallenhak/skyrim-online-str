#pragma once

#include <Services/ValidatedHitObservation.h>

/**
 * @brief A validated hit observation matched to one accepted canonical health
 * decrease for the same target lifecycle.
 *
 * This server-internal event has no client damage amount and does not establish
 * that the observation caused the decrease or that the target died.
 */
struct CorrelatedCombatObservationEvent final
{
    explicit constexpr CorrelatedCombatObservationEvent(const ValidatedHitObservation& acObservation) noexcept
        : Observation(acObservation)
    {
    }

    ValidatedHitObservation Observation;
};
