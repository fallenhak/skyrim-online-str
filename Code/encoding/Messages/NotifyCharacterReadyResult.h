#pragma once

#include "Message.h"

#include <Structs/CharacterReadyStatus.h>

struct NotifyCharacterReadyResult final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCharacterReadyResult;

    NotifyCharacterReadyResult()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyCharacterReadyResult& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Status == acRhs.Status; }

    CharacterReadyStatus Status{CharacterReadyStatus::kInvalidState};
};
