#pragma once

#include <Structs/GameId.h>
#include <Structs/AnimationVariables.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

struct FurnitureUsePolicy final
{
    // These are the synchronized boolean lookup positions in
    // AnimationGraphDescriptor_Master_Behavior. Keep this tied to that descriptor's ordered list.
    static constexpr std::size_t kIsInFurnitureBooleanIndex = 55;

    [[nodiscard]] static bool IsSeatEntryAction(std::string_view acEventName) noexcept
    {
        const bool isSeatEvent = acEventName.starts_with("IdleChair") || acEventName.starts_with("IdleStool") ||
                                 acEventName.starts_with("IdleSit") || acEventName.starts_with("IdleBarCounter") ||
                                 acEventName.starts_with("IdleJarlChair") || acEventName.starts_with("IdleTable");
        return isSeatEvent && acEventName.find("Enter") != std::string_view::npos;
    }

    [[nodiscard]] static bool IsFurnitureExitAction(std::string_view acEventName) noexcept
    {
        if (acEventName == "GetUpBegin" || acEventName == "GetUpExit" || acEventName == "ForceFurnExit" ||
            acEventName == "IdleChairExitToStand" || acEventName == "IdleFurnitureExitSlow" ||
            acEventName == "StoolBackExit" || acEventName == "TableBackExit")
            return true;

        return acEventName.starts_with("Idle") &&
               (acEventName.ends_with("Exit") || acEventName.ends_with("ExitStart"));
    }

    [[nodiscard]] static bool IsInFurniture(const AnimationVariables& acVariables) noexcept
    {
        return acVariables.Booleans.size() > kIsInFurnitureBooleanIndex &&
               acVariables.Booleans[kIsInFurnitureBooleanIndex];
    }

    [[nodiscard]] static bool CanEnter(
        const GameId& aFurnitureId,
        const bool aIsKnownFurniture,
        const bool aIsInRange,
        const bool aOccupiedByAnotherActor) noexcept
    {
        // Furniture the server has not been told about yet is still reservable, so a missed
        // discovery cannot stop players from sitting. Known furniture must be in range.
        return aFurnitureId && (!aIsKnownFurniture || aIsInRange) && !aOccupiedByAnotherActor;
    }

    static void ClearReservation(
        GameId& aFurnitureTargetId,
        GameId& aRejectedTargetId,
        bool& aHasEnteredFurniture,
        bool& aRejectedFurnitureSawActiveState) noexcept
    {
        aFurnitureTargetId = {};
        aRejectedTargetId = {};
        aHasEnteredFurniture = false;
        aRejectedFurnitureSawActiveState = false;
    }

    static void ClearRejectedTarget(GameId& aRejectedTargetId, bool& aRejectedFurnitureSawActiveState) noexcept
    {
        aRejectedTargetId = {};
        aRejectedFurnitureSawActiveState = false;
    }
};
