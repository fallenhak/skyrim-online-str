#pragma once

#include <Persistence/CharacterRecord.h>

#include <cmath>
#include <string_view>

namespace Persistence
{
/**
 * @brief The V1 subset of live character state that may be written back to persistence.
 *
 * Identity, level, and all progression/appearance data deliberately remain outside this
 * type. Health, magicka, and stamina are current vitals only; max/permanent values are not
 * persisted by this milestone.
 */
struct CharacterRuntimeState final
{
    GameId WorldSpace{};
    GameId Cell{};
    float PositionX{};
    float PositionY{};
    float PositionZ{};
    float Health{};
    float Magicka{};
    float Stamina{};
};

[[nodiscard]] inline bool IsValidCharacterRuntimeState(const CharacterId aCharacterId, const std::string_view acOwnerProfileId,
                                                        const CharacterRuntimeState& acState) noexcept
{
    if (aCharacterId <= 0 || acOwnerProfileId.empty() || acState.Cell == GameId{})
        return false;

    if (!std::isfinite(acState.PositionX) || !std::isfinite(acState.PositionY) || !std::isfinite(acState.PositionZ))
        return false;

    if (!std::isfinite(acState.Health) || !std::isfinite(acState.Magicka) || !std::isfinite(acState.Stamina))
        return false;

    return acState.Health >= 0.f && acState.Magicka >= 0.f && acState.Stamina >= 0.f;
}
} // namespace Persistence
