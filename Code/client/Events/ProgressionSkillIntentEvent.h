#pragma once

#include <Structs/Progression.h>

struct ProgressionSkillIntentEvent final
{
    explicit ProgressionSkillIntentEvent(const ProgressionSkill aSkill)
        : Skill(aSkill)
    {
    }

    ProgressionSkill Skill;
};
