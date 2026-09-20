#pragma once

#include "Message.h"

#include <Structs/CharacterSelectionStatus.h>

struct NotifyCharacterSelectionResult final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCharacterSelectionResult;

    NotifyCharacterSelectionResult()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyCharacterSelectionResult& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Status == acRhs.Status; }

    CharacterSelectionStatus Status{CharacterSelectionStatus::kInvalidState};
};
