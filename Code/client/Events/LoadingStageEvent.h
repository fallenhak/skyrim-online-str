#pragma once

#include <cstdint>

enum class LoadingStage : std::uint8_t
{
    kConnecting,
    kAuthenticating,
    kFetchingCharacters,
    kCreatingCharacter,
    kLoadingWorld,
    kApplyingCharacter,
    kRaceMenu,
    kEnteringWorld,
    kDone,
    // Connection lost in the world: frozen behind the entry screen while reconnecting.
    kReconnecting
};

struct LoadingStageEvent final
{
    LoadingStage Stage{};
    float Progress{};
};
