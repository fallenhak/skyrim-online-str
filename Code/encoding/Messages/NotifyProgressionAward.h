#pragma once

#include "Message.h"

#include <Structs/Progression.h>

#include <cstdint>

struct NotifyProgressionAward final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyProgressionAward;

    NotifyProgressionAward()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyProgressionAward& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && AwardId == acRhs.AwardId && CharacterId == acRhs.CharacterId && Skill == acRhs.Skill &&
            Experience == acRhs.Experience && Reason == acRhs.Reason;
    }

    std::uint64_t AwardId{};
    std::uint64_t CharacterId{};
    ProgressionSkill Skill{ProgressionSkill::kOneHanded};
    float Experience{};
    ProgressionAwardReason Reason{ProgressionAwardReason::kCreatureKill};
};
