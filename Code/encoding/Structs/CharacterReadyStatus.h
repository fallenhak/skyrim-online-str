#pragma once

#include <cstdint>

enum class CharacterReadyStatus : std::uint8_t
{
    kProceed = 0,
    kInvalidState,
    kCharacterMismatchOrUnavailable
};
