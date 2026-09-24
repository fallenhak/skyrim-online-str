#pragma once

#include <cstdint>

struct CharacterSlotsReceivedEvent final
{
    std::uint32_t Total{};
    std::uint32_t Unlocked{};
};
