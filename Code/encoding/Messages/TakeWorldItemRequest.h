#pragma once

#include "Message.h"

#include <Structs/GameId.h>

// A player picked up a stable, plugin-placed world item.
struct TakeWorldItemRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kTakeWorldItemRequest;

    TakeWorldItemRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const TakeWorldItemRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Id == acRhs.Id && CellId == acRhs.CellId && ActivatorId == acRhs.ActivatorId;
    }

    GameId Id{};
    GameId CellId{};
    uint32_t ActivatorId{};
};
