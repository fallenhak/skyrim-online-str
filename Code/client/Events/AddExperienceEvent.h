#pragma once

/**
 * @brief Deprecated legacy co-op XP event. Persistent progression no longer consumes it.
 */
struct AddExperienceEvent
{
    AddExperienceEvent(float aExperience)
        : Experience(aExperience)
    {
    }

    float Experience{};
};
