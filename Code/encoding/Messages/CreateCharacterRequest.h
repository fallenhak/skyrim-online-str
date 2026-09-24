#pragma once

#include "Message.h"

#include <cstdint>

struct CreateCharacterRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kCreateCharacterRequest;

    CreateCharacterRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const CreateCharacterRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && SlotIndex == acRhs.SlotIndex && Name == acRhs.Name; }

    std::uint32_t SlotIndex{};
    TiltedPhoques::String Name;
};
