#pragma once

#include "Message.h"

#include <cstdint>

struct NotifyCharacterSlots final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCharacterSlots;

    NotifyCharacterSlots()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyCharacterSlots& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Total == acRhs.Total && Unlocked == acRhs.Unlocked; }

    std::uint32_t Total{3};
    std::uint32_t Unlocked{1};
};
