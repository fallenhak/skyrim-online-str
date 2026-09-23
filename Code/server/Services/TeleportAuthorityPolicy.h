#pragma once

#include <Structs/CellMovementAuthorityPolicy.h>
#include <Structs/GameId.h>

#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>

struct TeleportAuthorityPolicy final
{
    [[nodiscard]] static constexpr bool CanRequestPartyTeleport(
        const bool aRequesterHasParty,
        const uint32_t aRequesterPartyId,
        const bool aTargetHasParty,
        const uint32_t aTargetPartyId) noexcept
    {
        return aRequesterHasParty && aTargetHasParty && aRequesterPartyId == aTargetPartyId;
    }

    [[nodiscard]] static bool HasValidDestination(
        const bool aHasCharacter,
        const bool aHasMovement,
        const GameId& acWorldSpaceId,
        const GameId& acCellId,
        const glm::vec3& acPosition) noexcept
    {
        return aHasCharacter && aHasMovement && CellMovementAuthorityPolicy::HasValidReferenceLocation(acWorldSpaceId, acCellId) &&
               CellMovementAuthorityPolicy::HasValidPosition(acPosition.x, acPosition.y, acPosition.z);
    }
};
