#pragma once

#include "Message.h"

struct RequestOwnershipClaim final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestOwnershipClaim;

    RequestOwnershipClaim()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestOwnershipClaim() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestOwnershipClaim& achRhs) const noexcept { return ServerId == achRhs.ServerId && ExpectedOwnershipEpoch == achRhs.ExpectedOwnershipEpoch && ForActivation == achRhs.ForActivation && GetOpcode() == achRhs.GetOpcode(); }

    uint32_t ServerId{};
    uint32_t ExpectedOwnershipEpoch{};
    // A local script activated this remote actor for the local player (an ambush trigger): the
    // player who set it off simulates it and replays the activation once it owns it.
    bool ForActivation{};
};
