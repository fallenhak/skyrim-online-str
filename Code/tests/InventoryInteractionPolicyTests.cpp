#include <Services/InventoryInteractionPolicy.h>
#include <Services/ObjectInteractionPolicy.h>

#include <catch2/catch.hpp>

#include <limits>

TEST_CASE("Inventory interaction permits current owner at the matching epoch", "[actor_authority]")
{
    REQUIRE(InventoryInteractionPolicy::IsAuthorized(true, true, true, false, false, true, true, true, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(true, true, false, false, false, true, true, true, false));
}

TEST_CASE("Inventory interaction permits only in-range non-persistent NPC exceptions", "[actor_authority]")
{
    REQUIRE(InventoryInteractionPolicy::IsAuthorized(true, false, true, false, false, true, false, false, true));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(true, false, false, false, false, true, false, false, true));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(true, false, true, false, false, true, false, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(true, false, true, false, false, true, true, false, true));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(true, false, true, false, false, true, false, true, true));
}

TEST_CASE("Inventory interaction does not treat arbitrary ownerless entities as objects", "[actor_authority]")
{
    REQUIRE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, true, false, false, false, true));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, true, false, false, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, false, true, true, false, false, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, true, false, false, false, false, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, true, true, false, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, true, false, true, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, true, false, false, true, false));
}

TEST_CASE("Equipment changes require a sender-owned character at its current epoch", "[actor_authority]")
{
    REQUIRE(InventoryInteractionPolicy::CanChangeEquipment(true, true, true, 7, 7));
    REQUIRE_FALSE(InventoryInteractionPolicy::CanChangeEquipment(true, true, true, 0, 7));
    REQUIRE_FALSE(InventoryInteractionPolicy::CanChangeEquipment(true, true, true, 6, 7));
    REQUIRE_FALSE(InventoryInteractionPolicy::CanChangeEquipment(true, true, false, 7, 7));

    // AssignObjects creates ownerless, non-character inventories. An epoch-zero
    // equipment request for one must be rejected before mutation or relay.
    REQUIRE_FALSE(InventoryInteractionPolicy::CanChangeEquipment(false, false, false, 0, 0));
    REQUIRE_FALSE(InventoryInteractionPolicy::CanChangeEquipment(false, true, true, 7, 7));
    REQUIRE_FALSE(InventoryInteractionPolicy::CanChangeEquipment(true, false, false, 0, 0));
}

TEST_CASE("A client-discovered provisional object rejects its later inventory delta", "[actor_authority]")
{
    // A client can propose a syntactically valid, nearby reference, but
    // AssignObjects creates it with HasTrustedState false. Its next ownerless
    // inventory request must not turn that discovery into shared server state.
    const GameId senderCell{0, 0x100};
    const GameId proposedObjectId{1, 0x200};
    const GridCellCoords interiorCoords{};
    REQUIRE(ObjectInteractionPolicy::CanDiscover(
        proposedObjectId, senderCell, {}, interiorCoords, senderCell, {}, interiorCoords));

    const bool objectHasTrustedState = false;
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(
        false, false, true, true, objectHasTrustedState, false, false, false, false));

    // A future server-baselined object remains eligible only in the sender's
    // stored location range.
    REQUIRE(InventoryInteractionPolicy::IsAuthorized(
        false, false, true, true, true, false, false, false, true));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(
        false, false, true, true, true, false, false, false, false));
}

TEST_CASE("Trusted ownerless object inventory requests require stored-location proximity", "[actor_authority]")
{
    const GameId senderCell{0, 1};
    const GameId objectCell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GridCellCoords senderCoords{10, -10};
    const GridCellCoords nearbyObjectCoords{12, -8};
    const GridCellCoords remoteObjectCoords{13, -10};

    const bool nearby = ObjectInteractionPolicy::CanInteract(
        objectCell, senderCell, worldSpace, senderCoords,
        objectCell, worldSpace, nearbyObjectCoords);
    const bool remote = ObjectInteractionPolicy::CanInteract(
        objectCell, senderCell, worldSpace, senderCoords,
        objectCell, worldSpace, remoteObjectCoords);

    REQUIRE(nearby);
    REQUIRE_FALSE(remote);
    REQUIRE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, true, false, false, false, nearby));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, true, false, false, false, remote));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, false, false, false, false, nearby));
}

TEST_CASE("Inventory interaction rejects malformed item payloads", "[actor_authority]")
{
    Inventory::Entry item{};
    item.BaseId.ModId = 1;
    item.BaseId.BaseId = 2;
    item.Count = 1;

    REQUIRE(InventoryInteractionPolicy::HasValidItemPayload(item));

    item.BaseId = GameId{};
    REQUIRE_FALSE(InventoryInteractionPolicy::HasValidItemPayload(item));

    item.BaseId.ModId = 1;
    item.BaseId.BaseId = 2;
    item.Count = 0;
    REQUIRE_FALSE(InventoryInteractionPolicy::HasValidItemPayload(item));

    item.Count = std::numeric_limits<int32_t>::min();
    REQUIRE_FALSE(InventoryInteractionPolicy::HasValidItemPayload(item));

    item.Count = 1;
    item.ExtraCharge = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(InventoryInteractionPolicy::HasValidItemPayload(item));

    item.ExtraCharge = 0.f;
    item.Count = -1;
    REQUIRE(InventoryInteractionPolicy::HasValidItemPayload(item));

    Inventory::EffectItem effect{};
    item.EnchantData.Effects.push_back(effect);
    item.EnchantData.Effects.front().RawCost = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(InventoryInteractionPolicy::HasValidItemPayload(item));
}

TEST_CASE("Inventory changes cannot create negative stacks or overflow existing stacks", "[actor_authority]")
{
    Inventory inventory{};
    Inventory::Entry item{};
    item.BaseId.ModId = 1;
    item.BaseId.BaseId = 2;

    item.Count = 1;
    REQUIRE(InventoryInteractionPolicy::CanApplyItem(inventory, item));

    item.Count = -1;
    REQUIRE_FALSE(InventoryInteractionPolicy::CanApplyItem(inventory, item));

    item.Count = 1;
    inventory.Entries.push_back(item);

    item.Count = -1;
    REQUIRE(InventoryInteractionPolicy::CanApplyItem(inventory, item));

    item.Count = -2;
    REQUIRE_FALSE(InventoryInteractionPolicy::CanApplyItem(inventory, item));

    item.Count = std::numeric_limits<int32_t>::max();
    REQUIRE_FALSE(InventoryInteractionPolicy::CanApplyItem(inventory, item));
}

TEST_CASE("Inventory notification flags preserve the NPC interaction semantics", "[actor_authority]")
{
    REQUIRE_FALSE(InventoryInteractionPolicy::ShouldNotifyClients(false, false));
    REQUIRE(InventoryInteractionPolicy::ShouldNotifyClients(true, false));
    REQUIRE(InventoryInteractionPolicy::ShouldNotifyClients(false, true));

    REQUIRE(InventoryInteractionPolicy::ShouldRelayDrop(true, true, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::ShouldRelayDrop(false, true, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::ShouldRelayDrop(true, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::ShouldRelayDrop(true, true, true));
}
