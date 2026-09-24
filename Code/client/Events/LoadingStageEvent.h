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
    kDone
};

struct LoadingStageEvent final
{
    LoadingStage Stage{};
    float Progress{};
};
