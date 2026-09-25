#include <Structs/CharacterLoadSnapshotValidation.h>

#include <cmath>

CharacterLoadSnapshotValidationError ValidateCharacterLoadSnapshot(const CharacterLoadSnapshot& acSnapshot) noexcept
{
    if (acSnapshot.CharacterId == 0)
        return CharacterLoadSnapshotValidationError::kInvalidCharacterId;

    // The persistence schema uses an int32 level. Keep the runtime boundary deliberately
    // conservative so malformed rows cannot reach the game API.
    if (acSnapshot.Level <= 0 || acSnapshot.Level > 1000)
        return CharacterLoadSnapshotValidationError::kInvalidLevel;

    if (acSnapshot.Sex != 0 && acSnapshot.Sex != 1)
        return CharacterLoadSnapshotValidationError::kUnsupportedSex;

    if (!acSnapshot.Race)
        return CharacterLoadSnapshotValidationError::kInvalidRace;

    if (!acSnapshot.CellId)
        return CharacterLoadSnapshotValidationError::kInvalidCell;

    if (!std::isfinite(acSnapshot.PositionX) || !std::isfinite(acSnapshot.PositionY) || !std::isfinite(acSnapshot.PositionZ))
        return CharacterLoadSnapshotValidationError::kInvalidPosition;

    if (!std::isfinite(acSnapshot.Health) || !std::isfinite(acSnapshot.Magicka) || !std::isfinite(acSnapshot.Stamina) || acSnapshot.Health < 0.f || acSnapshot.Magicka < 0.f || acSnapshot.Stamina < 0.f)
        return CharacterLoadSnapshotValidationError::kInvalidVitals;

    // Same bound the server stores (CharacterLookCodec::kMaxAppearanceBytes).
    if (acSnapshot.Appearance.size() > 64 * 1024)
        return CharacterLoadSnapshotValidationError::kInvalidAppearance;

    return CharacterLoadSnapshotValidationError::kNone;
}

bool IsCharacterLoadSnapshotValid(const CharacterLoadSnapshot& acSnapshot) noexcept
{
    return ValidateCharacterLoadSnapshot(acSnapshot) == CharacterLoadSnapshotValidationError::kNone;
}
