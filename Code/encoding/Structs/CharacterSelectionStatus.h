#pragma once

#include <cstdint>

enum class CharacterSelectionStatus : std::uint8_t
{
    kSuccess = 0,
    kIdentityNotReady,
    kNotFoundOrNotOwned,
    kInvalidState
};
