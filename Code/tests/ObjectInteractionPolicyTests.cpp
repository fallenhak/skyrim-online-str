#include <Services/ObjectInteractionPolicy.h>

#include <catch2/catch.hpp>

#include <cstddef>
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
    // A closed or closing door is the common case when a player opens it.
    REQUIRE(ObjectInteractionPolicy::IsValidOpenState(3));
    REQUIRE(ObjectInteractionPolicy::IsValidOpenState(4));
    REQUIRE_FALSE(ObjectInteractionPolicy::IsValidOpenState(5));
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
        true, true, true, objectCell, senderCell, worldSpace, nearCoords,
        objectCell, worldSpace, objectCoords, objectCell, worldSpace, objectCoords));

    REQUIRE_FALSE(ObjectInteractionPolicy::CanActivate(
        true, true, true, objectCell, senderCell, worldSpace, nearCoords,
        remoteActorCell, worldSpace, GridCellCoords{20, -8}, objectCell, worldSpace, objectCoords));

    // Client activation notifications are replayed through the same game hook on
    // observers. A nearby non-owner replay must not become a new server action.
    REQUIRE_FALSE(ObjectInteractionPolicy::CanActivate(
        true, true, false, objectCell, senderCell, worldSpace, nearCoords,
        objectCell, worldSpace, objectCoords, objectCell, worldSpace, objectCoords));
}

TEST_CASE("A client-discovered provisional object cannot relay a forged activation", "[object_authority]")
{
    const GameId senderCell{0, 1};
    const GameId objectCell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GridCellCoords coords{10, -10};
    const GameId objectId{1, 0x200};

    REQUIRE(ObjectInteractionPolicy::CanDiscover(
        objectId, senderCell, worldSpace, coords, objectCell, worldSpace, coords));

    const bool objectHasTrustedState = false; // AssignObjects creates discovered references provisionally.
    std::size_t peerNotificationCount = 0;
    const bool shouldRelay = ObjectInteractionPolicy::CanActivate(
        objectHasTrustedState, true, true, objectCell, senderCell, worldSpace, coords,
        objectCell, worldSpace, coords, objectCell, worldSpace, coords);
    if (shouldRelay)
        ++peerNotificationCount;

    REQUIRE_FALSE(shouldRelay);
    REQUIRE(peerNotificationCount == 0);
}

TEST_CASE("A client-reported lock result needs more than a trusted baseline", "[object_authority]")
{
    // OnLockChange has no server-side lock outcome resolver, so its client
    // report cannot mutate canonical state or reach observers even if a
    // trusted baseline is added later.
    const auto assertRejectedWithoutValidatedOutcome = [](const bool hasTrustedState)
    {
        LockData canonicalState{};
        canonicalState.IsLocked = true;
        canonicalState.LockLevel = 50;
        bool observerStateIsLocked = true;
        std::size_t relayCount = 0;

        REQUIRE_FALSE(ObjectInteractionPolicy::TryHandleLockChange(
            hasTrustedState, false, canonicalState, false, 0,
            [&]
            {
                ++relayCount;
                observerStateIsLocked = false;
            }));
        REQUIRE(canonicalState.IsLocked);
        REQUIRE(canonicalState.LockLevel == 50);
        REQUIRE(observerStateIsLocked);
        REQUIRE(relayCount == 0);
    };

    assertRejectedWithoutValidatedOutcome(false); // Provisional object remains rejected.
    assertRejectedWithoutValidatedOutcome(true);  // A baseline alone cannot validate a client result.
}

TEST_CASE("A harvestable object is harvested once and relayed once", "[object_authority][harvest]")
{
    const GameId senderCell{0, 1};
    const GameId objectCell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GridCellCoords coords{10, -10};

    bool harvested = false;
    std::size_t relayCount = 0;
    const auto relay = [&] { ++relayCount; };

    REQUIRE(ObjectInteractionPolicy::TryHarvest(
        true, harvested, true, true, objectCell, senderCell, worldSpace, coords,
        objectCell, worldSpace, coords, objectCell, worldSpace, coords, relay));
    REQUIRE(harvested);
    REQUIRE(relayCount == 1);

    // A second player activating the same flora gets nothing relayed.
    REQUIRE_FALSE(ObjectInteractionPolicy::TryHarvest(
        true, harvested, true, true, objectCell, senderCell, worldSpace, coords,
        objectCell, worldSpace, coords, objectCell, worldSpace, coords, relay));
    REQUIRE(harvested);
    REQUIRE(relayCount == 1);
}

TEST_CASE("Harvest rejects non-harvestable, foreign or out-of-range activations", "[object_authority][harvest]")
{
    const GameId senderCell{0, 1};
    const GameId objectCell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GridCellCoords coords{10, -10};
    const GridCellCoords farCoords{20, -10};

    const auto assertRejected = [&](const bool harvestable, const bool actorExists, const bool owned,
                                    const GameId& requestedCell, const GridCellCoords& activatorCoords)
    {
        bool harvested = false;
        std::size_t relayCount = 0;
        REQUIRE_FALSE(ObjectInteractionPolicy::TryHarvest(
            harvestable, harvested, actorExists, owned, requestedCell, senderCell, worldSpace, coords,
            objectCell, worldSpace, activatorCoords, objectCell, worldSpace, coords, [&] { ++relayCount; }));
        REQUIRE_FALSE(harvested);
        REQUIRE(relayCount == 0);
    };

    assertRejected(false, true, true, objectCell, coords);  // doors/containers keep the A09 trusted-state gate
    assertRejected(true, false, true, objectCell, coords);  // unknown activator
    assertRejected(true, true, false, objectCell, coords);  // activator owned by another player
    assertRejected(true, true, true, GameId{0, 9}, coords); // forged cell
    assertRejected(true, true, true, objectCell, farCoords); // activator too far from the object
}

TEST_CASE("Harvested objects respawn after the configured delay", "[object_authority][harvest]")
{
    constexpr std::uint64_t cHarvestTick = 100;
    const std::uint64_t respawnAt = ObjectInteractionPolicy::HarvestRespawnTick(cHarvestTick, false);
    REQUIRE(respawnAt == cHarvestTick + 30 * 60);
    REQUIRE(ObjectInteractionPolicy::HarvestRespawnTick(cHarvestTick, true) == cHarvestTick + 60 * 60);

    REQUIRE_FALSE(ObjectInteractionPolicy::IsHarvestRespawnDue(true, respawnAt, respawnAt - 1));
    REQUIRE(ObjectInteractionPolicy::IsHarvestRespawnDue(true, respawnAt, respawnAt));
    REQUIRE(ObjectInteractionPolicy::IsHarvestRespawnDue(true, respawnAt, respawnAt + 50));

    // Nothing to respawn when the object was never harvested.
    REQUIRE_FALSE(ObjectInteractionPolicy::IsHarvestRespawnDue(false, respawnAt, respawnAt + 50));
}

TEST_CASE("A respawned object can be harvested again", "[object_authority][harvest]")
{
    const GameId cell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GridCellCoords coords{10, -10};
    const auto harvest = [&](bool& harvested)
    {
        return ObjectInteractionPolicy::TryHarvest(
            true, harvested, true, true, cell, cell, worldSpace, coords,
            cell, worldSpace, coords, cell, worldSpace, coords, [] {});
    };

    bool harvested = false;
    REQUIRE(harvest(harvested));
    const std::uint64_t respawnAt = ObjectInteractionPolicy::HarvestRespawnTick(0, false);
    REQUIRE_FALSE(harvest(harvested));

    REQUIRE(ObjectInteractionPolicy::IsHarvestRespawnDue(harvested, respawnAt, respawnAt));
    harvested = false;
    REQUIRE(harvest(harvested));
}

namespace
{
struct DoorFixture
{
    GameId Cell{0, 2};
    GameId WorldSpace{0, 0x3C};
    GridCellCoords Coords{10, -10};
    DoorState State{};
    std::size_t Relays{};

    bool Toggle(const uint8_t aPreOpenState, const bool aIsDoor = true, const bool aOwned = true)
    {
        return ObjectInteractionPolicy::TryToggleDoor(
            aIsDoor, State, aPreOpenState, true, aOwned, Cell, Cell, WorldSpace, Coords,
            Cell, WorldSpace, Coords, Cell, WorldSpace, Coords, [&] { ++Relays; });
    }
};

constexpr uint8_t kNone = 0;
constexpr uint8_t kOpen = 1;
constexpr uint8_t kClosed = 3;
} // namespace

TEST_CASE("The server owns door open state and relays each valid toggle", "[object_authority][door]")
{
    DoorFixture door;
    REQUIRE_FALSE(door.State.IsKnown);

    // First activation of a closed door: learned, flipped to open, relayed.
    REQUIRE(door.Toggle(kClosed));
    REQUIRE(door.State.IsKnown);
    REQUIRE(door.State.IsOpen);
    REQUIRE(door.Relays == 1);

    REQUIRE(door.Toggle(kOpen));
    REQUIRE_FALSE(door.State.IsOpen);
    REQUIRE(door.Relays == 2);
}

TEST_CASE("A toggle from a client that saw a stale door state is rejected", "[object_authority][door]")
{
    DoorFixture door;
    REQUIRE(door.Toggle(kClosed)); // now open on the server

    // A second client still sees it closed and tries to open it.
    REQUIRE_FALSE(door.Toggle(kClosed));
    REQUIRE(door.State.IsOpen);
    REQUIRE(door.Relays == 1);
}

TEST_CASE("Door toggle keeps the activation checks", "[object_authority][door]")
{
    DoorFixture notDoor;
    REQUIRE_FALSE(notDoor.Toggle(kClosed, false));

    DoorFixture foreign;
    REQUIRE_FALSE(foreign.Toggle(kClosed, true, false));

    DoorFixture noState; // kNone: load doors and doors without open animation
    REQUIRE_FALSE(noState.Toggle(kNone));

    for (const auto* pDoor : {&notDoor, &foreign, &noState})
    {
        REQUIRE_FALSE(pDoor->State.IsKnown);
        REQUIRE(pDoor->Relays == 0);
    }
}
