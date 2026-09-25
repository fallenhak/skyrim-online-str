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

/**
 * @brief Why a captured runtime state may or may not be written back.
 *
 * Dead is split out from Invalid because a dead player (Skyrim drives current health below
 * zero on death) is a normal, transient game state: the save is skipped, not an error.
 */
enum class CharacterRuntimeStateVerdict
{
    Valid,
    Dead,
    Invalid,
};

[[nodiscard]] inline CharacterRuntimeStateVerdict EvaluateCharacterRuntimeState(const CharacterId aCharacterId, const std::string_view acOwnerProfileId,
                                                                               const CharacterRuntimeState& acState) noexcept
{
    if (aCharacterId <= 0 || acOwnerProfileId.empty() || acState.Cell == GameId{})
        return CharacterRuntimeStateVerdict::Invalid;

    if (!std::isfinite(acState.PositionX) || !std::isfinite(acState.PositionY) || !std::isfinite(acState.PositionZ))
        return CharacterRuntimeStateVerdict::Invalid;

    if (!std::isfinite(acState.Health) || !std::isfinite(acState.Magicka) || !std::isfinite(acState.Stamina))
        return CharacterRuntimeStateVerdict::Invalid;

    if (acState.Magicka < 0.f || acState.Stamina < 0.f)
        return CharacterRuntimeStateVerdict::Invalid;

    return acState.Health < 0.f ? CharacterRuntimeStateVerdict::Dead : CharacterRuntimeStateVerdict::Valid;
}

[[nodiscard]] inline bool IsValidCharacterRuntimeState(const CharacterId aCharacterId, const std::string_view acOwnerProfileId,
                                                        const CharacterRuntimeState& acState) noexcept
{
    return EvaluateCharacterRuntimeState(aCharacterId, acOwnerProfileId, acState) == CharacterRuntimeStateVerdict::Valid;
}
} // namespace Persistence
