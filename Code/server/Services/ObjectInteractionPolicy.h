#pragma once

#include <Structs/GameId.h>
#include <Structs/GridCellCoords.h>
#include <Structs/LockData.h>

#include <cstdint>
#include <limits>

struct ObjectInteractionPolicy final
{
    [[nodiscard]] static bool IsInSenderRange(
        const GameId& aSenderCell,
        const GameId& aSenderWorldSpace,
        const GridCellCoords& aSenderCoords,
        const GameId& aObjectCell,
        const GameId& aObjectWorldSpace,
        const GridCellCoords& aObjectCoords) noexcept
    {
        if (!HasValidFormId(aSenderCell) || !HasValidFormId(aObjectCell))
            return false;

        if (!aSenderWorldSpace || !aObjectWorldSpace)
            return !aSenderWorldSpace && !aObjectWorldSpace && aSenderCell == aObjectCell;

        if (!HasValidFormId(aSenderWorldSpace) || !HasValidFormId(aObjectWorldSpace) || aSenderWorldSpace != aObjectWorldSpace ||
            !HasUsableCoords(aSenderCoords) || !HasUsableCoords(aObjectCoords))
            return false;

        constexpr int64_t cRange = GridCellCoords::m_gridsToLoad / 2;
        return IsAxisInRange(aSenderCoords.X, aObjectCoords.X, cRange) && IsAxisInRange(aSenderCoords.Y, aObjectCoords.Y, cRange);
    }

    [[nodiscard]] static bool CanDiscover(
        const GameId& aObjectId,
        const GameId& aSenderCell,
        const GameId& aSenderWorldSpace,
        const GridCellCoords& aSenderCoords,
        const GameId& aObjectCell,
        const GameId& aObjectWorldSpace,
        const GridCellCoords& aObjectCoords) noexcept
    {
        return HasValidFormId(aObjectId) && IsInSenderRange(aSenderCell, aSenderWorldSpace, aSenderCoords, aObjectCell, aObjectWorldSpace, aObjectCoords);
    }

    [[nodiscard]] static bool CanInteract(
        const GameId& aRequestedCell,
        const GameId& aSenderCell,
        const GameId& aSenderWorldSpace,
        const GridCellCoords& aSenderCoords,
        const GameId& aObjectCell,
        const GameId& aObjectWorldSpace,
        const GridCellCoords& aObjectCoords) noexcept
    {
        return HasValidFormId(aRequestedCell) && aRequestedCell == aObjectCell &&
            IsInSenderRange(aSenderCell, aSenderWorldSpace, aSenderCoords, aObjectCell, aObjectWorldSpace, aObjectCoords);
    }

    [[nodiscard]] static constexpr bool IsAuthorizedActivator(const bool aActorExists, const bool aOwnedBySender) noexcept
    {
        return aActorExists && aOwnedBySender;
    }

    [[nodiscard]] static bool CanActivate(
        const bool aActorExists,
        const bool aOwnedBySender,
        const GameId& aRequestedCell,
        const GameId& aSenderCell,
        const GameId& aSenderWorldSpace,
        const GridCellCoords& aSenderCoords,
        const GameId& aActivatorCell,
        const GameId& aActivatorWorldSpace,
        const GridCellCoords& aActivatorCoords,
        const GameId& aObjectCell,
        const GameId& aObjectWorldSpace,
        const GridCellCoords& aObjectCoords) noexcept
    {
        return IsAuthorizedActivator(aActorExists, aOwnedBySender) &&
            CanInteract(
                aRequestedCell, aSenderCell, aSenderWorldSpace, aSenderCoords,
                aObjectCell, aObjectWorldSpace, aObjectCoords) &&
            IsInSenderRange(
                aActivatorCell, aActivatorWorldSpace, aActivatorCoords,
                aObjectCell, aObjectWorldSpace, aObjectCoords);
    }

    [[nodiscard]] static constexpr bool IsValidOpenState(const uint8_t aOpenState) noexcept
    {
        // TESObjectREFR::OpenState defines kNone, kOpen, and kOpening.
        return aOpenState <= 2;
    }

    [[nodiscard]] static bool TryApplyLockChange(
        const bool aHasTrustedState,
        LockData& aCurrentState,
        const bool aIsLocked,
        const uint8_t aLockLevel) noexcept
    {
        if (!aHasTrustedState)
            return false;

        aCurrentState.IsLocked = aIsLocked;
        aCurrentState.LockLevel = aLockLevel;
        return true;
    }

private:
    [[nodiscard]] static bool HasValidFormId(const GameId& aId) noexcept
    {
        return aId && aId.BaseId != 0;
    }

    [[nodiscard]] static bool HasUsableCoords(const GridCellCoords& aCoords) noexcept
    {
        constexpr auto cUnset = std::numeric_limits<int32_t>::max();
        return aCoords.X != cUnset && aCoords.Y != cUnset;
    }

    [[nodiscard]] static bool IsAxisInRange(const int32_t aFirst, const int32_t aSecond, const int64_t aRange) noexcept
    {
        const int64_t difference = static_cast<int64_t>(aFirst) - static_cast<int64_t>(aSecond);
        return difference >= -aRange && difference <= aRange;
    }
};
