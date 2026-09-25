#pragma once

#include "Message.h"

#include <Structs/Tints.h>

#include <cstdint>

// The local player's look after RaceMenu: the same appearance data the player's spawn carries,
// sent again because the assignment happened before RaceMenu and carried the default NPC.
struct UpdatePlayerAppearanceRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kUpdatePlayerAppearanceRequest;

    UpdatePlayerAppearanceRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const UpdatePlayerAppearanceRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ChangeFlags == acRhs.ChangeFlags && AppearanceBuffer == acRhs.AppearanceBuffer && FaceTints == acRhs.FaceTints;
    }

    uint32_t ChangeFlags{};
    String AppearanceBuffer{};
    Tints FaceTints{};
};
