#include <Services/ObjectInteractionPolicy.h>

#include <catch2/catch.hpp>

#include <limits>

TEST_CASE("Object discovery requires a valid form and sender cell range", "[object_authority]")
{
    const GameId interiorCell{0, 0x100};
    const GameId otherInteriorCell{0, 0x101};
    const GameId objectId{1, 0x200};
    const GridCellCoords unusedCoords{};
    const GridCellCoords noWorldSpace{};

    REQUIRE(ObjectInteractionPolicy::CanDiscover(objectId, interiorCell, {}, unusedCoords, interiorCell, {}, noWorldSpace));
    REQUIRE_FALSE(ObjectInteractionPolicy::CanDiscover({}, interiorCell, {}, unusedCoords, interiorCell, {}, noWorldSpace));
    REQUIRE_FALSE(ObjectInteractionPolicy::CanDiscover(GameId{1, 0}, interiorCell, {}, unusedCoords, interiorCell, {}, noWorldSpace));
    REQUIRE_FALSE(ObjectInteractionPolicy::CanDiscover(objectId, interiorCell, {}, unusedCoords, otherInteriorCell, {}, noWorldSpace));
}

TEST_CASE("Object discovery rejects a forged out-of-range location", "[object_authority]")
{
    const GameId senderCell{0, 1};
    const GameId objectCell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GameId otherWorldSpace{0, 0x3D};
    const GameId objectId{1, 0x200};
    const GridCellCoords senderCoords{10, -10};

    REQUIRE(ObjectInteractionPolicy::CanDiscover(objectId, senderCell, worldSpace, senderCoords, objectCell, worldSpace, GridCellCoords{12, -8}));
    REQUIRE_FALSE(ObjectInteractionPolicy::CanDiscover(objectId, senderCell, worldSpace, senderCoords, objectCell, worldSpace, GridCellCoords{13, -10}));
    REQUIRE_FALSE(ObjectInteractionPolicy::CanDiscover(objectId, senderCell, worldSpace, senderCoords, objectCell, otherWorldSpace, GridCellCoords{10, -10}));

    const auto unset = std::numeric_limits<int32_t>::max();
    REQUIRE_FALSE(ObjectInteractionPolicy::CanDiscover(objectId, senderCell, worldSpace, GridCellCoords{unset, 0}, objectCell, worldSpace, GridCellCoords{10, -10}));
    REQUIRE_FALSE(ObjectInteractionPolicy::CanDiscover(
        objectId, senderCell, worldSpace, GridCellCoords{std::numeric_limits<int32_t>::min(), 0},
        objectCell, worldSpace, GridCellCoords{std::numeric_limits<int32_t>::max() - 1, 0}));
}

TEST_CASE("Known object interactions require the stored cell and sender range", "[object_authority]")
{
    const GameId senderCell{0, 1};
    const GameId objectCell{0, 2};
    const GameId otherCell{0, 3};
    const GameId worldSpace{0, 0x3C};
    const GridCellCoords senderCoords{10, -10};
    const GridCellCoords objectCoords{12, -8};

    REQUIRE(ObjectInteractionPolicy::CanInteract(objectCell, senderCell, worldSpace, senderCoords, objectCell, worldSpace, objectCoords));
    REQUIRE_FALSE(ObjectInteractionPolicy::CanInteract(otherCell, senderCell, worldSpace, senderCoords, objectCell, worldSpace, objectCoords));
    REQUIRE_FALSE(ObjectInteractionPolicy::CanInteract(objectCell, senderCell, worldSpace, senderCoords, objectCell, worldSpace, GridCellCoords{13, -8}));
}

TEST_CASE("Object activation requires an owned actor and valid open state", "[object_authority]")
{
    REQUIRE(ObjectInteractionPolicy::IsAuthorizedActivator(true, true));
    REQUIRE_FALSE(ObjectInteractionPolicy::IsAuthorizedActivator(false, true));
    REQUIRE_FALSE(ObjectInteractionPolicy::IsAuthorizedActivator(true, false));

    REQUIRE(ObjectInteractionPolicy::IsValidOpenState(0));
    REQUIRE(ObjectInteractionPolicy::IsValidOpenState(1));
    REQUIRE(ObjectInteractionPolicy::IsValidOpenState(2));
    REQUIRE_FALSE(ObjectInteractionPolicy::IsValidOpenState(3));
    REQUIRE_FALSE(ObjectInteractionPolicy::IsValidOpenState(std::numeric_limits<uint8_t>::max()));
}

TEST_CASE("Activation requires a nearby sender-owned actor", "[object_authority]")
{
    const GameId senderCell{0, 1};
    const GameId objectCell{0, 2};
    const GameId remoteActorCell{0, 3};
    const GameId worldSpace{0, 0x3C};
    const GridCellCoords nearCoords{10, -10};
    const GridCellCoords objectCoords{12, -8};

    REQUIRE(ObjectInteractionPolicy::CanActivate(
        true, true, objectCell, senderCell, worldSpace, nearCoords,
        objectCell, worldSpace, objectCoords, objectCell, worldSpace, objectCoords));

    REQUIRE_FALSE(ObjectInteractionPolicy::CanActivate(
        true, true, objectCell, senderCell, worldSpace, nearCoords,
        remoteActorCell, worldSpace, GridCellCoords{20, -8}, objectCell, worldSpace, objectCoords));

    // Client activation notifications are replayed through the same game hook on
    // observers. A nearby non-owner replay must not become a new server action.
    REQUIRE_FALSE(ObjectInteractionPolicy::CanActivate(
        true, false, objectCell, senderCell, worldSpace, nearCoords,
        objectCell, worldSpace, objectCoords, objectCell, worldSpace, objectCoords));
}

TEST_CASE("A provisional object's forged lock report is not applied or relayed", "[object_authority]")
{
    // OnLockChange applies this policy before it constructs NotifyLockChange
    // or sends to peers. A forged unlock on a provisional reference leaves the
    // canonical lock and observers' door state locked.
    LockData canonicalState{};
    canonicalState.IsLocked = true;
    canonicalState.LockLevel = 50;
    bool observerStateIsLocked = true;

    const bool shouldRelay = ObjectInteractionPolicy::TryApplyLockChange(false, canonicalState, false, 0);
    if (shouldRelay)
        observerStateIsLocked = false;

    REQUIRE_FALSE(shouldRelay);
    REQUIRE(canonicalState.IsLocked);
    REQUIRE(canonicalState.LockLevel == 50);
    REQUIRE(observerStateIsLocked);

    REQUIRE(ObjectInteractionPolicy::TryApplyLockChange(true, canonicalState, false, 0));
    REQUIRE_FALSE(canonicalState.IsLocked);
    REQUIRE(canonicalState.LockLevel == 0);
}
