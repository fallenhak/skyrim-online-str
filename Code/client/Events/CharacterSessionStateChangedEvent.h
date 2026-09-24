#pragma once

#include <cstdint>

enum class ClientCharacterSessionState : std::uint8_t
{
    kDisconnected = 0,
    kAwaitingCharacterSelection,
    kCharacterSelected,
    kApplyingCharacter,
    kAwaitingClientReady,
    kAwaitingPlayerAssignment,
    kInWorld
};

struct CharacterSessionStateChangedEvent final
{
    ClientCharacterSessionState State;
};
