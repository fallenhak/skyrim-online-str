#pragma once

#include "Message.h"

#include <Structs/GameId.h>

// Server-owned state of a stable world item after a player takes it.
struct NotifyWorldItemTaken final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyWorldItemTaken;

    NotifyWorldItemTaken()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyWorldItemTaken& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Id == acRhs.Id; }

    GameId Id{};
};
