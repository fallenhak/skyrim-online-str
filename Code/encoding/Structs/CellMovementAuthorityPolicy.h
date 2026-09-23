#pragma once

#include <Structs/GameId.h>
#include <Structs/GridCellCoords.h>
#include <Structs/Vector3_NetQuantize.h>

#include <cmath>
#include <cstdint>
#include <limits>

struct CellMovementAuthorityPolicy final
{
    [[nodiscard]] static bool HasValidInteriorCell(const GameId& acCellId) noexcept
    {
        return static_cast<bool>(acCellId);
    }

    [[nodiscard]] static bool HasValidExteriorCell(
        const GameId& acWorldSpaceId, const GameId& acCellId, const GridCellCoords& acCenterCoords) noexcept
    {
        return static_cast<bool>(acWorldSpaceId) && static_cast<bool>(acCellId) && !IsUnset(acCenterCoords);
    }

    [[nodiscard]] static bool HasValidReferenceLocation(const GameId& acWorldSpaceId, const GameId& acCellId) noexcept
    {
        // Exterior temporary cells may not have a mapped cell ID, but their worldspace
        // still provides a usable range context. Interiors need their cell ID.
        return static_cast<bool>(acWorldSpaceId) || static_cast<bool>(acCellId);
    }

    [[nodiscard]] static bool HasValidReportedLocation(
        const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) noexcept
    {
        return HasValidReferenceLocation(acWorldSpaceId, acCellId) && HasValidPosition(acPosition.x, acPosition.y, acPosition.z);
    }

    [[nodiscard]] static bool HasValidPosition(const float aX, const float aY, const float aZ) noexcept
    {
        return std::isfinite(aX) && std::isfinite(aY) && std::isfinite(aZ) &&
               GridCellCoords::CalculateGridCellCoords(aX, aY) != GridCellCoords{};
    }

private:
    [[nodiscard]] static bool IsUnset(const GridCellCoords& acCoords) noexcept
    {
        const auto sentinel = std::numeric_limits<int32_t>::max();
        return acCoords.X == sentinel || acCoords.Y == sentinel;
    }
};
