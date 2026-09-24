#pragma once

#include "Message.h"

#include <cstdint>

struct NotifyCharacterEnteredWorld final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCharacterEnteredWorld;

    NotifyCharacterEnteredWorld()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyCharacterEnteredWorld& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && CharacterId == acRhs.CharacterId; }

    std::uint64_t CharacterId{};
};
