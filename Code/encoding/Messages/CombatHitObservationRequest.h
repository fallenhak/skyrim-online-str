#pragma once

#include "Message.h"

#include <cstdint>

/**
 * @brief Client report that a player character observed a hit on an actor.
 *
 * This request carries no damage, persistent character identity, classification,
 * or kill claim. The server must resolve both entities and their current
 * authority/lifecycle state before retaining it as a pending observation.
 */
struct CombatHitObservationRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kCombatHitObservationRequest;

    CombatHitObservationRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    [[nodiscard]] bool operator==(const CombatHitObservationRequest& acRhs) const noexcept
    {
        return AttackerServerId == acRhs.AttackerServerId && AttackerOwnershipEpoch == acRhs.AttackerOwnershipEpoch &&
               TargetServerId == acRhs.TargetServerId && TargetLifecycleGeneration == acRhs.TargetLifecycleGeneration &&
               ObservationId == acRhs.ObservationId && GetOpcode() == acRhs.GetOpcode();
    }

    std::uint32_t AttackerServerId{};
    std::uint32_t AttackerOwnershipEpoch{};
    std::uint32_t TargetServerId{};
    std::uint64_t TargetLifecycleGeneration{};
    std::uint64_t ObservationId{};
};
