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
               ObservationId == acRhs.ObservationId && m_hasValidWireEncoding == acRhs.m_hasValidWireEncoding &&
               GetOpcode() == acRhs.GetOpcode();
    }

    [[nodiscard]] bool IsWellFormed() const noexcept
    {
        return m_hasValidWireEncoding && AttackerServerId != 0 && AttackerOwnershipEpoch != 0 && TargetServerId != 0 &&
               TargetLifecycleGeneration != 0 && ObservationId != 0 && AttackerServerId != TargetServerId;
    }

    std::uint32_t AttackerServerId{};
    std::uint32_t AttackerOwnershipEpoch{};
    std::uint32_t TargetServerId{};
    std::uint64_t TargetLifecycleGeneration{};
    std::uint64_t ObservationId{};

private:
    // The three server identifiers and the ownership epoch are wire varints,
    // but their protocol fields are 32-bit. Remember overflow so narrowing
    // cannot silently alias a different server entity or owner epoch.
    bool m_hasValidWireEncoding{true};
};
