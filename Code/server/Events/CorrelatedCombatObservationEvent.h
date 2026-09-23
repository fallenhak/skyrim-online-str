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
        : AttackerServerId(acObservation.AttackerServerId)
        , AttackerOwnershipEpoch(acObservation.AttackerOwnershipEpoch)
        , TargetServerId(acObservation.TargetServerId)
        , TargetLifecycleGeneration(acObservation.TargetLifecycleGeneration)
        , ObservationId(acObservation.ObservationId)
        , ObservedTick(acObservation.ObservedTick)
    {
    }

    [[nodiscard]] constexpr ValidatedHitObservation ToObservation() const noexcept
    {
        return {AttackerServerId, AttackerOwnershipEpoch, TargetServerId,
                TargetLifecycleGeneration, ObservationId, ObservedTick};
    }

    // Triggered EnTT event payloads must be assignable. The source DTO stays
    // immutable; this event stores its identity-only value fields directly.
    ValidatedHitObservation::ServerId AttackerServerId;
    ValidatedHitObservation::OwnershipEpoch AttackerOwnershipEpoch;
    ValidatedHitObservation::ServerId TargetServerId;
    ValidatedHitObservation::LifecycleGeneration TargetLifecycleGeneration;
    ValidatedHitObservation::ObservationSequence ObservationId;
    ValidatedHitObservation::ObservationTick ObservedTick;
};
