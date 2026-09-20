#pragma once

#include "Message.h"

#include <Structs/CharacterLoadSnapshot.h>

struct NotifyCharacterLoadSnapshot final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCharacterLoadSnapshot;

    NotifyCharacterLoadSnapshot()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyCharacterLoadSnapshot& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Snapshot == acRhs.Snapshot; }

    CharacterLoadSnapshot Snapshot{};
};
