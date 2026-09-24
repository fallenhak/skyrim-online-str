#pragma once

#include "Message.h"

#include <Structs/GameId.h>

#include <cstdint>

struct UpdateCharacterAppearanceRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kUpdateCharacterAppearanceRequest;

    UpdateCharacterAppearanceRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const UpdateCharacterAppearanceRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Race == acRhs.Race && Sex == acRhs.Sex; }

    GameId Race{};
    std::int32_t Sex{};
};
