#pragma once

#include <Structs/CharacterSelectionStatus.h>

struct CharacterSelectionResultEvent final
{
    CharacterSelectionStatus Status{CharacterSelectionStatus::kInvalidState};
};
