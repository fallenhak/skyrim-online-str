#pragma once

#include <Structs/Progression.h>

#include <cstdint>

struct ProgressionAwardAppliedEvent final
{
    std::uint64_t AwardId{};
    std::uint64_t CharacterId{};
    ProgressionSkill Skill{ProgressionSkill::kOneHanded};
    float Experience{};
    ProgressionAwardReason Reason{ProgressionAwardReason::kCreatureKill};
};
