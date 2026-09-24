#pragma once

#include <cstdint>

enum class CharacterAssignmentRejectReason : std::uint8_t
{
    kPopulationHumanoidDenied,
    kPopulationUnknownDenied,
};
