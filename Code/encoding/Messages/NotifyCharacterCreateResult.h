#pragma once

#include "Message.h"

#include <Structs/CharacterCreateStatus.h>

#include <cstdint>

struct NotifyCharacterCreateResult final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCharacterCreateResult;

    NotifyCharacterCreateResult()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyCharacterCreateResult& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Status == acRhs.Status && CharacterId == acRhs.CharacterId; }

    CharacterCreateStatus Status{CharacterCreateStatus::kError};
    std::uint64_t CharacterId{};
};
