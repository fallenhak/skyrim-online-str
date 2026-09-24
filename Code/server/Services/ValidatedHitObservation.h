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
 * boundary, and no client-selected identity is carried forward here. The
 * attacker lifecycle generation is captured by the server at admission; it
 * prevents a pending observation from rebinding if an entity ID is reused.
 * ObservationId is scoped to the attacker authority incarnation, while
 * ObservedTick is ordering metadata only.
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
        const ObservationTick aObservedTick,
        const LifecycleGeneration aAttackerLifecycleGeneration) noexcept
        : AttackerServerId(aAttackerServerId)
        , AttackerOwnershipEpoch(aAttackerOwnershipEpoch)
        , TargetServerId(aTargetServerId)
        , TargetLifecycleGeneration(aTargetLifecycleGeneration)
        , ObservationId(aObservationId)
        , ObservedTick(aObservedTick)
        , AttackerLifecycleGeneration(aAttackerLifecycleGeneration)
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
               TargetServerId != kInvalidServerId && AttackerServerId != TargetServerId &&
               TargetLifecycleGeneration != kInvalidLifecycleGeneration && ObservationId != kInvalidObservationId &&
               ObservedTick != 0 && AttackerLifecycleGeneration != kInvalidLifecycleGeneration;
    }

    [[nodiscard]] constexpr bool IsFromAttackerIncarnation(const LifecycleGeneration aCurrentGeneration) const noexcept
    {
        return aCurrentGeneration != kInvalidLifecycleGeneration && AttackerLifecycleGeneration == aCurrentGeneration;
    }

    friend constexpr bool operator==(const ValidatedHitObservation&, const ValidatedHitObservation&) noexcept = default;

    const ServerId AttackerServerId;
    const OwnershipEpoch AttackerOwnershipEpoch;
    const ServerId TargetServerId;
    const LifecycleGeneration TargetLifecycleGeneration;
    const ObservationSequence ObservationId;
    const ObservationTick ObservedTick;
    const LifecycleGeneration AttackerLifecycleGeneration;
};
