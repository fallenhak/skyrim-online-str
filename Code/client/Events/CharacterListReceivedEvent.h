#pragma once

#include <Structs/CharacterSummary.h>

#include <vector>

struct CharacterListReceivedEvent final
{
    std::vector<CharacterSummary> Characters;
};
