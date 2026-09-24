#pragma once

#include "Message.h"

#include <Structs/CharacterAssignmentRejectReason.h>

#include <cstdint>

struct NotifyCharacterAssignmentRejected final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCharacterAssignmentRejected;

    NotifyCharacterAssignmentRejected()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyCharacterAssignmentRejected& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Cookie == acRhs.Cookie && Reason == acRhs.Reason;
    }

    std::uint32_t Cookie{};
    CharacterAssignmentRejectReason Reason{CharacterAssignmentRejectReason::kPopulationUnknownDenied};
};
