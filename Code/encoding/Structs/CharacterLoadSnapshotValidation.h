#pragma once

#include <Structs/CharacterLoadSnapshot.h>

#include <cstdint>

enum class CharacterLoadSnapshotValidationError : std::uint8_t
{
    kNone = 0,
    kInvalidCharacterId,
    kInvalidLevel,
    kUnsupportedSex,
    kInvalidRace,
    kInvalidCell,
    kInvalidWorldSpace,
    kInvalidPosition,
    kInvalidVitals,
    kInvalidAppearance
};

[[nodiscard]] CharacterLoadSnapshotValidationError ValidateCharacterLoadSnapshot(const CharacterLoadSnapshot& acSnapshot) noexcept;
[[nodiscard]] bool IsCharacterLoadSnapshotValid(const CharacterLoadSnapshot& acSnapshot) noexcept;
