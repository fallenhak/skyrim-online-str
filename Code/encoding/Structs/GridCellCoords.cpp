#include <Structs/GridCellCoords.h>
#include <TiltedCore/Serialization.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

using TiltedPhoques::Serialization;

namespace
{
bool IsUnset(const GridCellCoords& acCoords) noexcept
{
    // INT32_MAX is the unset sentinel; reject a partial sentinel as well.
    return acCoords.X == std::numeric_limits<int32_t>::max() || acCoords.Y == std::numeric_limits<int32_t>::max();
}

bool IsWithinDistance(const int32_t aLeft, const int32_t aRight, const int32_t aDistance) noexcept
{
    const int64_t difference = static_cast<int64_t>(aLeft) - static_cast<int64_t>(aRight);
    return difference >= -static_cast<int64_t>(aDistance) && difference <= static_cast<int64_t>(aDistance);
}

bool TryToGridCoordinate(const float aCoordinate, int32_t& aGridCoordinate) noexcept
{
    if (!std::isfinite(aCoordinate))
        return false;

    const double grid = std::floor(static_cast<double>(aCoordinate) / 4096.0);
    // Avoid overflowing the conversion or turning an out-of-range position into a plausible edge cell.
    if (grid < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
        grid >= static_cast<double>(std::numeric_limits<int32_t>::max()))
        return false;

    aGridCoordinate = static_cast<int32_t>(grid);
    return true;
}
} // namespace

GridCellCoords::GridCellCoords()
{
    Reset();
}

GridCellCoords::GridCellCoords(int32_t aX, int32_t aY) noexcept
    : X(aX)
    , Y(aY)
{
}

bool GridCellCoords::operator==(const GridCellCoords& acRhs) const noexcept
{
    return X == acRhs.X && Y == acRhs.Y;
}

bool GridCellCoords::operator!=(const GridCellCoords& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void GridCellCoords::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, X);
    Serialization::WriteVarInt(aWriter, Y);
}

void GridCellCoords::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    X = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Y = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}

GridCellCoords GridCellCoords::CalculateGridCellCoords(const Vector3_NetQuantize& aCoords) noexcept
{
    return CalculateGridCellCoords(aCoords.x, aCoords.y);
}

GridCellCoords GridCellCoords::CalculateGridCellCoords(const float aX, const float aY) noexcept
{
    int32_t gridX{};
    int32_t gridY{};
    if (!TryToGridCoordinate(aX, gridX) || !TryToGridCoordinate(aY, gridY))
        return {};

    return GridCellCoords(gridX, gridY);
}

bool GridCellCoords::AreGridCellsOverlapping(const GridCellCoords& aCoords1, const GridCellCoords& aCoords2) noexcept
{
    return !IsUnset(aCoords1) && !IsUnset(aCoords2) && IsWithinDistance(aCoords1.X, aCoords2.X, m_gridsToLoad - 1) &&
           IsWithinDistance(aCoords1.Y, aCoords2.Y, m_gridsToLoad - 1);
}

bool GridCellCoords::IsCellInGridCell(const GridCellCoords& aCell, const GridCellCoords& aGridCell, bool aIsDragon) noexcept
{
    if (IsUnset(aCell) || IsUnset(aGridCell))
        return false;

    int32_t gridsToLoad = aIsDragon ? m_gridsToLoadIfDragon : m_gridsToLoad;
    int32_t distanceToBorder = gridsToLoad / 2;
    return IsWithinDistance(aCell.X, aGridCell.X, distanceToBorder) && IsWithinDistance(aCell.Y, aGridCell.Y, distanceToBorder);
}
