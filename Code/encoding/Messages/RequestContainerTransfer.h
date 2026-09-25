#pragma once

#include "Message.h"

#include <Structs/Inventory.h>

// Moves Item.Count units between a synced container and the sender's character in one
// server step. Direction: 0 = take (container -> player), 1 = put (player -> container).
// ExpectedContainerCount is what the sender saw in the container before the move.
struct RequestContainerTransfer final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestContainerTransfer;

    RequestContainerTransfer()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestContainerTransfer& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && RequestId == acRhs.RequestId && ContainerId == acRhs.ContainerId && Direction == acRhs.Direction &&
            ExpectedContainerCount == acRhs.ExpectedContainerCount && Item == acRhs.Item;
    }

    uint32_t RequestId{};
    uint32_t ContainerId{};
    uint8_t Direction{};
    int32_t ExpectedContainerCount{};
    Inventory::Entry Item{};
};
