#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/CharacterAssignmentRejectReason.h>

struct PopulationSuppressedComponent
{
    explicit PopulationSuppressedComponent(const CharacterAssignmentRejectReason aReason) noexcept
        : Reason(aReason)
    {
    }

    CharacterAssignmentRejectReason Reason;
};
