#pragma once

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Stl.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/MovementPayloadLimits.h>
#include <Structs/ReferenceUpdate.h>
#include <Structs/CellMovementAuthorityPolicy.h>

#include <cmath>
#include <cstdint>

struct MovementAuthorityPolicy final
{
    [[nodiscard]] static constexpr bool IsAuthorized(
        const bool aEntityExists,
        const bool aOwnerExists,
        const bool aIsCurrentOwner,
        const uint32_t aOwnershipEpoch) noexcept
    {
        return aEntityExists && aOwnerExists && aIsCurrentOwner && aOwnershipEpoch != 0;
    }

    [[nodiscard]] static bool HasValidPayload(const ReferenceUpdate& acUpdate) noexcept
    {
        if (acUpdate.OwnershipEpoch == 0 || acUpdate.ActionEvents.size() > MovementPayloadLimits::kMaxActionEvents)
            return false;

        const auto& movement = acUpdate.UpdatedMovement;
        if (!CellMovementAuthorityPolicy::HasValidReferenceLocation(movement.WorldSpaceId, movement.CellId) ||
            !CellMovementAuthorityPolicy::HasValidPosition(movement.Position.x, movement.Position.y, movement.Position.z) ||
            !std::isfinite(movement.Rotation.x) || !std::isfinite(movement.Rotation.y) || !std::isfinite(movement.Direction) ||
            !movement.Variables.HasFiniteValues())
            return false;

        for (const auto& action : acUpdate.ActionEvents)
        {
            if (!action.Variables.HasFiniteValues())
                return false;
        }

        return true;
    }
};
