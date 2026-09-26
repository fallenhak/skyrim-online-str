#pragma once

#include "Message.h"

#include <Structs/Inventory.h>

// An actor's inventory as the server now records it, relayed from its simulating owner.
struct NotifyActorInventory final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyActorInventory;

    NotifyActorInventory()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyActorInventory& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && OwnershipEpoch == acRhs.OwnershipEpoch && Contents == acRhs.Contents;
    }

    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    Inventory Contents{};
};
