#include <Services/InventoryInteractionPolicy.h>

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
    REQUIRE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, true, false, false, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, false, true, true, false, false, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, true, false, false, false, false, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, true, true, false, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, true, false, true, false, false));
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(false, false, true, true, true, false, false, true, false));
}

TEST_CASE("A discovered provisional object rejects later inventory deltas", "[actor_authority]")
{
    // AssignObjects creates this object with HasTrustedState false. Its later
    // ownerless inventory request must not turn that forged discovery into
    // shared server inventory state.
    REQUIRE_FALSE(InventoryInteractionPolicy::IsAuthorized(
        false, false, true, true, false, false, false, false, false));

    // The gate leaves any future server-baselined object eligible for the
    // existing shared-object path.
    REQUIRE(InventoryInteractionPolicy::IsAuthorized(
        false, false, true, true, true, false, false, false, false));
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
