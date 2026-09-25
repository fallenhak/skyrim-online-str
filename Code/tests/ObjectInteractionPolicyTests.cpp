#include <TiltedCore/Stl.hpp>

#include <Services/ObjectInteractionPolicy.h>
#include <Structs/GameId.h>
#include <Structs/GridCellCoords.h>

#include <catch2/catch.hpp>

#include <cstddef>
#include <cstdint>
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

TEST_CASE("Activation notifications reach peers in the same exterior range", "[object_authority][activation]")
{
    const GameId objectCell{0, 1};
    const GameId peerCell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GridCellCoords objectCoords{10, -10};

    // Observers may occupy a different exterior cell while still having this object in range.
    REQUIRE(ObjectInteractionPolicy::CanInteract(
        objectCell, peerCell, worldSpace, GridCellCoords{12, -8},
        objectCell, worldSpace, objectCoords));
    REQUIRE_FALSE(ObjectInteractionPolicy::CanInteract(
        objectCell, peerCell, worldSpace, GridCellCoords{13, -10},
        objectCell, worldSpace, objectCoords));
    REQUIRE_FALSE(ObjectInteractionPolicy::CanInteract(
        objectCell, peerCell, GameId{0, 0x3D}, GridCellCoords{10, -10},
        objectCell, worldSpace, objectCoords));

    const GameId interiorCell{0, 0x100};
    const GameId otherInteriorCell{0, 0x101};
    REQUIRE(ObjectInteractionPolicy::CanInteract(
        interiorCell, interiorCell, {}, {}, interiorCell, {}, {}));
    REQUIRE_FALSE(ObjectInteractionPolicy::CanInteract(
        interiorCell, otherInteriorCell, {}, {}, interiorCell, {}, {}));
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

TEST_CASE("A client-reported lock needs more than a trusted baseline", "[object_authority]")
{
    // OnLockChange has no server-side lock outcome resolver, so a reported
    // lock (or level change) cannot mutate canonical state or reach observers.
    const auto assertLockRejected = [](const bool hasTrustedState)
    {
        LockData canonicalState{};
        canonicalState.IsLocked = false;
        canonicalState.LockLevel = 50;
        std::size_t relayCount = 0;

        REQUIRE_FALSE(ObjectInteractionPolicy::TryHandleLockChange(
            hasTrustedState, false, canonicalState, true, 100, [&] { ++relayCount; }));
        REQUIRE_FALSE(canonicalState.IsLocked);
        REQUIRE(canonicalState.LockLevel == 50);
        REQUIRE(relayCount == 0);
    };

    assertLockRejected(false);
    assertLockRejected(true);
}

TEST_CASE("A reported unlock is accepted and relayed once", "[object_authority]")
{
    for (const bool hasTrustedState : {false, true})
    {
        LockData canonicalState{};
        canonicalState.IsLocked = true;
        canonicalState.LockLevel = 50;
        std::size_t relayCount = 0;
        const auto relay = [&] { ++relayCount; };

        REQUIRE(ObjectInteractionPolicy::TryHandleLockChange(hasTrustedState, false, canonicalState, false, 0, relay));
        REQUIRE_FALSE(canonicalState.IsLocked);
        REQUIRE(canonicalState.LockLevel == 50);
        REQUIRE(relayCount == 1);

        // A repeated unlock of a known-open object is not relayed again.
        REQUIRE(ObjectInteractionPolicy::TryHandleLockChange(hasTrustedState, false, canonicalState, false, 0, relay));
        REQUIRE(relayCount == (hasTrustedState ? 1u : 2u));
    }
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

TEST_CASE("Object state notifications include peers in nearby cells of the same world", "[object_authority]")
{
    const GameId peerCell{0, 1};
    const GameId objectCell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GameId otherWorldSpace{0, 0x3D};

    REQUIRE(ObjectInteractionPolicy::IsInSenderRange(
        peerCell, worldSpace, GridCellCoords{10, -10}, objectCell, worldSpace, GridCellCoords{12, -8}));
    REQUIRE_FALSE(ObjectInteractionPolicy::IsInSenderRange(
        peerCell, otherWorldSpace, GridCellCoords{10, -10}, objectCell, worldSpace, GridCellCoords{12, -8}));
}

TEST_CASE("World loot is taken once by a nearby owner and relayed once", "[object_authority][world_loot]")
{
    const GameId senderCell{0, 1};
    const GameId objectCell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GridCellCoords coords{10, -10};

    bool taken = false;
    std::size_t relayCount = 0;
    const auto relay = [&] { ++relayCount; };

    REQUIRE(ObjectInteractionPolicy::TryTakeWorldItem(
        true, true, taken, true, true, objectCell, senderCell, worldSpace, coords,
        objectCell, worldSpace, coords, objectCell, worldSpace, coords, relay));
    REQUIRE(taken);
    REQUIRE(relayCount == 1);

    REQUIRE_FALSE(ObjectInteractionPolicy::TryTakeWorldItem(
        true, true, taken, true, true, objectCell, senderCell, worldSpace, coords,
        objectCell, worldSpace, coords, objectCell, worldSpace, coords, relay));
    REQUIRE(taken);
    REQUIRE(relayCount == 1);
}

TEST_CASE("World loot rejects containers, foreign actors, forged cells and distant pickups", "[object_authority][world_loot]")
{
    const GameId senderCell{0, 1};
    const GameId objectCell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GridCellCoords coords{10, -10};
    const GridCellCoords farCoords{20, -10};

    const auto assertRejected = [&](const bool trustedObjectState, const bool openLoot, const bool actorExists, const bool owned,
                                    const GameId& requestedCell, const GridCellCoords& activatorCoords)
    {
        bool taken = false;
        std::size_t relayCount = 0;
        REQUIRE_FALSE(ObjectInteractionPolicy::TryTakeWorldItem(
            trustedObjectState, openLoot, taken, actorExists, owned, requestedCell, senderCell, worldSpace, coords,
            objectCell, worldSpace, activatorCoords, objectCell, worldSpace, coords,
            [&] { ++relayCount; }));
        REQUIRE_FALSE(taken);
        REQUIRE(relayCount == 0);
    };

    assertRejected(true, false, true, true, objectCell, coords);  // containers and corpses never enter the open-loot path
    assertRejected(false, true, true, true, objectCell, coords);  // client-discovered type and placement are not trusted
    assertRejected(true, true, false, true, objectCell, coords);
    assertRejected(true, true, true, false, objectCell, coords);
    assertRejected(true, true, true, true, GameId{0, 9}, coords);
    assertRejected(true, true, true, true, objectCell, farCoords);
}

TEST_CASE("Only changed world objects survive cell cleanup", "[object_authority][world_state]")
{
    REQUIRE(ObjectInteractionPolicy::ShouldRetainWorldState(true, 0, false, 0, false, 0, 100)); // touched door, even when closed
    REQUIRE(ObjectInteractionPolicy::ShouldRetainWorldState(false, 2, false, 0, false, 0, 100)); // activator history
    REQUIRE(ObjectInteractionPolicy::ShouldRetainWorldState(false, 0, true, 200, false, 0, 100)); // loot timer is active
    REQUIRE(ObjectInteractionPolicy::ShouldRetainWorldState(false, 0, false, 0, true, 200, 100)); // harvest timer is active
    REQUIRE_FALSE(ObjectInteractionPolicy::ShouldRetainWorldState(false, 0, false, 0, false, 0, 100)); // untouched
    REQUIRE_FALSE(ObjectInteractionPolicy::ShouldRetainWorldState(false, 0, true, 200, false, 0, 200)); // expired loot
    REQUIRE_FALSE(ObjectInteractionPolicy::ShouldRetainWorldState(false, 0, false, 0, true, 200, 201)); // expired harvest
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
    constexpr std::uint64_t cHarvestUnix = 100;
    const std::uint64_t respawnAt = ObjectInteractionPolicy::HarvestRespawnAtUnix(cHarvestUnix, false);
    REQUIRE(respawnAt == cHarvestUnix + 30 * 60);
    REQUIRE(ObjectInteractionPolicy::HarvestRespawnAtUnix(cHarvestUnix, true) == cHarvestUnix + 60 * 60);
    REQUIRE(ObjectInteractionPolicy::ItemRespawnAtUnix(cHarvestUnix) == cHarvestUnix + ObjectInteractionPolicy::kItemRespawnTicks);

    REQUIRE_FALSE(ObjectInteractionPolicy::IsHarvestRespawnDue(true, respawnAt, respawnAt - 1));
    REQUIRE(ObjectInteractionPolicy::IsHarvestRespawnDue(true, respawnAt, respawnAt));
    REQUIRE(ObjectInteractionPolicy::IsHarvestRespawnDue(true, respawnAt, respawnAt + 50));

    // Nothing to respawn when the object was never harvested.
    REQUIRE_FALSE(ObjectInteractionPolicy::IsHarvestRespawnDue(false, respawnAt, respawnAt + 50));
    REQUIRE_FALSE(ObjectInteractionPolicy::IsLootRespawnDue(false, respawnAt, respawnAt + 50));
    REQUIRE(ObjectInteractionPolicy::IsLootRespawnDue(true, respawnAt, respawnAt));
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
    const std::uint64_t respawnAt = ObjectInteractionPolicy::HarvestRespawnAtUnix(0, false);
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

namespace
{
struct ActivatorFixture
{
    GameId Cell{0, 2};
    GameId WorldSpace{0, 0x3C};
    GridCellCoords Coords{10, -10};
    ActivatorState State{};
    std::size_t Relays{};

    bool Activate(const std::uint64_t aTick, const bool aIsActivator = true, const bool aOwned = true, const GridCellCoords aActorCoords = {10, -10})
    {
        return ObjectInteractionPolicy::TryRelayActivator(
            aIsActivator, State, aTick, true, aOwned, Cell, Cell, WorldSpace, Coords,
            Cell, WorldSpace, aActorCoords, Cell, WorldSpace, Coords, [&] { ++Relays; });
    }
};
} // namespace

TEST_CASE("Each accepted activator activation is counted and relayed", "[object_authority][activator]")
{
    ActivatorFixture lever;
    REQUIRE(lever.Activate(5));
    REQUIRE(lever.Activate(6));
    REQUIRE(lever.Activate(40));
    REQUIRE(lever.State.ActivationCount == 3);
    REQUIRE(lever.State.LastActivationTick == 40);
    REQUIRE(lever.Relays == 3);
}

TEST_CASE("A second activation in the same server second is rejected", "[object_authority][activator]")
{
    ActivatorFixture lever;
    REQUIRE(lever.Activate(5));
    REQUIRE_FALSE(lever.Activate(5)); // another player pulled it in the same tick
    REQUIRE(lever.State.ActivationCount == 1);
    REQUIRE(lever.Relays == 1);

    // The very first activation is never blocked by the cooldown, even at tick 0.
    ActivatorFixture fresh;
    REQUIRE(fresh.Activate(0));
}

TEST_CASE("Activator relay keeps the activation checks", "[object_authority][activator]")
{
    ActivatorFixture notActivator;
    REQUIRE_FALSE(notActivator.Activate(1, false));

    ActivatorFixture foreign;
    REQUIRE_FALSE(foreign.Activate(1, true, false));

    ActivatorFixture far;
    REQUIRE_FALSE(far.Activate(1, true, true, GridCellCoords{40, 40}));

    for (const auto* pFixture : {&notActivator, &foreign, &far})
    {
        REQUIRE(pFixture->State.ActivationCount == 0);
        REQUIRE(pFixture->Relays == 0);
    }
}
