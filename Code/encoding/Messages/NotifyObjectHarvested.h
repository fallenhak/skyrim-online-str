#pragma once

#include "Message.h"

#include <Structs/GameId.h>

// Server-owned harvest state of flora or a placed ingredient changed.
struct NotifyObjectHarvested final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyObjectHarvested;
    NotifyObjectHarvested()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyObjectHarvested& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Id == acRhs.Id && IsHarvested == acRhs.IsHarvested; }

    GameId Id{};
    bool IsHarvested{};
};
