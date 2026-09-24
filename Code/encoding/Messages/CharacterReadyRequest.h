#pragma once

#include "Message.h"

#include <cstdint>

struct CharacterReadyRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kCharacterReadyRequest;

    CharacterReadyRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const CharacterReadyRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && CharacterId == acRhs.CharacterId; }

    std::uint64_t CharacterId{};
};
