#pragma once

#include <Structs/CharacterCreateStatus.h>

#include <cstdint>

struct CharacterCreateResultEvent final
{
    CharacterCreateStatus Status{CharacterCreateStatus::kError};
    std::uint64_t CharacterId{};
};
