#pragma once

#include <cstdint>

/**
 * @brief One server-validated hit observation, captured without applying damage.
 *
 * This is an internal value object, not a network message or a damage command.
 * A caller constructs it only after resolving the sender and the canonical
 * attacker/target entities.  Its identity fields are immutable so an accepted
 * observation can only be appended to a bounded pending-observation store; a
 * later policy may expire or remove the whole record, but cannot rewrite it.
 *
 * The observation deliberately contains no persistent character identifier.
 * The server may resolve that identity later, after the attacker authorization
 * boundary, and no client-selected identity is carried forward here.
 * ObservationId is scoped to the attacker authority incarnation, while
 * ObservedTick is ordering metadata only; neither is a damage or reward value.
 */
struct ValidatedHitObservation final
{
    using ServerId = std::uint32_t;
    using OwnershipEpoch = std::uint32_t;
    using LifecycleGeneration = std::uint64_t;
    using ObservationSequence = std::uint64_t;
    using ObservationTick = std::uint64_t;

    static constexpr ServerId kInvalidServerId = 0;
    static constexpr OwnershipEpoch kInvalidOwnershipEpoch = 0;
    static constexpr LifecycleGeneration kInvalidLifecycleGeneration = 0;
    static constexpr ObservationSequence kInvalidObservationId = 0;

    constexpr ValidatedHitObservation(
        const ServerId aAttackerServerId,
        const OwnershipEpoch aAttackerOwnershipEpoch,
        const ServerId aTargetServerId,
        const LifecycleGeneration aTargetLifecycleGeneration,
        const ObservationSequence aObservationId,
        const ObservationTick aObservedTick) noexcept
        : AttackerServerId(aAttackerServerId)
        , AttackerOwnershipEpoch(aAttackerOwnershipEpoch)
        , TargetServerId(aTargetServerId)
        , TargetLifecycleGeneration(aTargetLifecycleGeneration)
        , ObservationId(aObservationId)
        , ObservedTick(aObservedTick)
    {
    }

    /**
     * @brief Checks only the DTO's identity invariants.
     *
     * This does not authorize an attacker, classify a target, prove range or
     * collision, correlate health, or establish a kill. Those checks belong to
     * later server-side policies.
     */
    [[nodiscard]] constexpr bool IsWellFormed() const noexcept
    {
        return AttackerServerId != kInvalidServerId && AttackerOwnershipEpoch != kInvalidOwnershipEpoch &&
               TargetServerId != kInvalidServerId && TargetLifecycleGeneration != kInvalidLifecycleGeneration &&
               ObservationId != kInvalidObservationId;
    }

    friend constexpr bool operator==(const ValidatedHitObservation&, const ValidatedHitObservation&) noexcept = default;

    const ServerId AttackerServerId;
    const OwnershipEpoch AttackerOwnershipEpoch;
    const ServerId TargetServerId;
    const LifecycleGeneration TargetLifecycleGeneration;
    const ObservationSequence ObservationId;
    const ObservationTick ObservedTick;
};
