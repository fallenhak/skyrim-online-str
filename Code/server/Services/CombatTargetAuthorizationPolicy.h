#pragma once

#include <Components.h>

#include <cstdint>
#include <limits>

/**
 * @brief Canonical server facts used to authorize a hit observation's target.
 *
 * The target id and observed lifecycle generation come from the observation;
 * the entity, lifecycle, population identity, and cells must be resolved from
 * the server's current world. None of these facts may be inferred from a
 * client-supplied Creature/Humanoid classification.
 */
struct CombatTargetAuthorizationInput final
{
    using ServerId = std::uint32_t;
    using LifecycleGeneration = ActorLifecycleComponent::Generation;

    ServerId TargetServerId{};
    bool TargetEntityExists{};
    LifecycleGeneration ObservedTargetLifecycleGeneration{};
    const ActorLifecycleComponent* pCurrentTargetLifecycle{};
    const ActorPopulationIdentityComponent* pTargetPopulationIdentity{};
    const CellIdComponent* pAttackerCell{};
    const CellIdComponent* pTargetCell{};
};

/**
 * @brief Pure eligibility check for a server-resolved combat target.
 *
 * This only accepts trusted server-classified Creatures, whose population
 * class excludes players and humanoid NPCs. Cell checks establish plausible
 * replication-range overlap; they do not prove collision, line of sight, or
 * exact weapon reach.
 */
struct CombatTargetAuthorizationPolicy final
{
    [[nodiscard]] static bool IsAuthorized(const CombatTargetAuthorizationInput& acInput) noexcept
    {
        if (acInput.TargetServerId == 0 || !acInput.TargetEntityExists || acInput.ObservedTargetLifecycleGeneration == 0 ||
            acInput.pCurrentTargetLifecycle == nullptr || !acInput.pCurrentTargetLifecycle->IsValid() ||
            acInput.pCurrentTargetLifecycle->GetGeneration() != acInput.ObservedTargetLifecycleGeneration ||
            acInput.pTargetPopulationIdentity == nullptr || !acInput.pTargetPopulationIdentity->IsTrustedCreature())
            return false;

        return acInput.pAttackerCell != nullptr && acInput.pTargetCell != nullptr &&
               AreCellsPlausiblyInRange(*acInput.pAttackerCell, *acInput.pTargetCell);
    }

    /**
     * @brief Checks that both canonical cells are valid and replication-near.
     *
     * Interior actors must share the same cell. Exterior actors must share a
     * worldspace and fall within the ordinary 5x5 loaded-grid window. The
     * dragon-expanded window is deliberately not used because the dragon flag
     * is client-reported. Coordinate deltas are widened before subtraction so
     * malformed extreme values cannot overflow signed 32-bit arithmetic.
     */
    [[nodiscard]] static bool AreCellsPlausiblyInRange(const CellIdComponent& acAttackerCell, const CellIdComponent& acTargetCell) noexcept
    {
        if (!HasValidCell(acAttackerCell) || !HasValidCell(acTargetCell))
            return false;

        const bool attackerIsInterior = acAttackerCell.IsInInteriorCell();
        const bool targetIsInterior = acTargetCell.IsInInteriorCell();
        if (attackerIsInterior != targetIsInterior)
            return false;

        if (attackerIsInterior)
            return acAttackerCell.Cell == acTargetCell.Cell;

        if (acAttackerCell.WorldSpaceId != acTargetCell.WorldSpaceId || !HasValidGridCoordinates(acAttackerCell.CenterCoords) ||
            !HasValidGridCoordinates(acTargetCell.CenterCoords))
            return false;

        return AreGridCoordinatesNear(acTargetCell.CenterCoords, acAttackerCell.CenterCoords);
    }

private:
    [[nodiscard]] static bool HasValidCell(const CellIdComponent& acCell) noexcept
    {
        return acCell.Cell != GameId{} && (acCell.IsInInteriorCell() || acCell.WorldSpaceId != GameId{});
    }

    [[nodiscard]] static bool HasValidGridCoordinates(const GridCellCoords& acCoords) noexcept
    {
        constexpr auto invalid = std::numeric_limits<std::int32_t>::max();
        return acCoords.X != invalid && acCoords.Y != invalid;
    }

    [[nodiscard]] static bool AreGridCoordinatesNear(const GridCellCoords& acLeft, const GridCellCoords& acRight) noexcept
    {
        constexpr std::int64_t radius = GridCellCoords::m_gridsToLoad / 2;
        const auto deltaX = static_cast<std::int64_t>(acLeft.X) - static_cast<std::int64_t>(acRight.X);
        const auto deltaY = static_cast<std::int64_t>(acLeft.Y) - static_cast<std::int64_t>(acRight.Y);
        return deltaX >= -radius && deltaX <= radius && deltaY >= -radius && deltaY <= radius;
    }
};
