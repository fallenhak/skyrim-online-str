#pragma once

#include "Message.h"

#include <cstdint>

struct SelectCharacterRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kSelectCharacterRequest;

    SelectCharacterRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const SelectCharacterRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && CharacterId == acRhs.CharacterId; }

    std::uint64_t CharacterId{};
};
