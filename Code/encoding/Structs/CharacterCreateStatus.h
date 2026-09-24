#pragma once

#include <cstdint>

enum class CharacterCreateStatus : std::uint8_t
{
    kSuccess = 0,
    kNameInvalid = 1,
    kNameTaken = 2,
    kSlotLocked = 3,
    kSlotOccupied = 4,
    kError = 5
};
