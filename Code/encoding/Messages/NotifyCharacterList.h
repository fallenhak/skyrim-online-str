#pragma once

#include "Message.h"

#include <Structs/CharacterSummary.h>

#include <vector>

struct NotifyCharacterList final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCharacterList;

    NotifyCharacterList()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyCharacterList& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Characters == acRhs.Characters; }

    std::vector<CharacterSummary> Characters;
};
