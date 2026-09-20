#pragma once

#include <Structs/CharacterLoadSnapshotValidation.h>

#include <TiltedCore/Stl.hpp>

struct CharacterSnapshotApplyFailedEvent final
{
    CharacterLoadSnapshotValidationError Error{CharacterLoadSnapshotValidationError::kNone};
    TiltedPhoques::String Reason;
};
