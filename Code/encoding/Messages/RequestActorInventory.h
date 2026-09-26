#pragma once

#include "Message.h"

#include <Structs/Inventory.h>

// The simulating owner's full inventory of an actor after the engine rebuilt it (a leveled actor
// conformed to the server's pick rolls a new inventory that single item changes never describe).
struct RequestActorInventory final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestActorInventory;

    RequestActorInventory()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestActorInventory& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && OwnershipEpoch == acRhs.OwnershipEpoch && Contents == acRhs.Contents;
    }

    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    Inventory Contents{};
};
