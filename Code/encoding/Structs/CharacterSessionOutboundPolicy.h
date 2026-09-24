#pragma once

#include <Opcodes.h>

#include <cstdint>

enum class CharacterClientSessionPhase : std::uint8_t
{
    kDisconnected = 0,
    kAwaitingCharacterSelection,
    kApplyingCharacter,
    kAwaitingClientReady,
    kAwaitingPlayerAssignment,
    kInWorld
};

[[nodiscard]] bool CanSendCharacterProtocolMessage(
    ClientOpcode aOpcode, CharacterClientSessionPhase aPhase, bool aIsLocalPlayerAssignment) noexcept;
