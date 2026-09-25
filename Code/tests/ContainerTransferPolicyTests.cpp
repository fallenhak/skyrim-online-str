#include <Services/ContainerTransferPolicy.h>

#include <catch2/catch.hpp>

namespace
{
Inventory::Entry Item(const int32_t aCount, const uint32_t aBaseId = 0x13989)
{
    Inventory::Entry entry{};
    entry.BaseId = GameId{0, aBaseId};
    entry.Count = aCount;
    return entry;
}

Inventory Holding(const int32_t aCount, const uint32_t aBaseId = 0x13989)
{
    Inventory inventory{};
    if (aCount > 0)
        inventory.Entries.push_back(Item(aCount, aBaseId));
    return inventory;
}

constexpr auto kTake = ContainerTransferDirection::kTake;
constexpr auto kPut = ContainerTransferDirection::kPut;
constexpr auto kAccepted = ContainerTransferResult::kAccepted;
} // namespace

TEST_CASE("Taking from a container moves the item in one step", "[container_transfer]")
{
    ContainerTransferSession session;
    Inventory chest = Holding(3);
    Inventory player = Holding(0);

    REQUIRE(ContainerTransferPolicy::TryTransfer(session, 1, 10, true, kTake, Item(2), 3, chest, player) == kAccepted);
    REQUIRE(ContainerTransferPolicy::CountOf(chest, Item(1)) == 1);
    REQUIRE(ContainerTransferPolicy::CountOf(player, Item(1)) == 2);

    REQUIRE(ContainerTransferPolicy::TryTransfer(session, 2, 10, true, kPut, Item(2), 1, chest, player) == kAccepted);
    REQUIRE(ContainerTransferPolicy::CountOf(chest, Item(1)) == 3);
    REQUIRE(ContainerTransferPolicy::CountOf(player, Item(1)) == 0);
}

TEST_CASE("Two players taking the last item: the first wins, the second is stale and nothing is duplicated", "[container_transfer]")
{
    ContainerTransferSession alice;
    ContainerTransferSession bob;
    Inventory chest = Holding(1);
    Inventory alicePack = Holding(0);
    Inventory bobPack = Holding(0);

    // Both clients saw one item in the chest and moved it locally.
    REQUIRE(ContainerTransferPolicy::TryTransfer(alice, 7, 10, true, kTake, Item(1), 1, chest, alicePack) == kAccepted);
    REQUIRE(ContainerTransferPolicy::TryTransfer(bob, 7, 10, true, kTake, Item(1), 1, chest, bobPack) == ContainerTransferResult::kStale);

    // Bob's client rolls its local move back; the server never gave him the item.
    const int64_t total = ContainerTransferPolicy::CountOf(chest, Item(1)) + ContainerTransferPolicy::CountOf(alicePack, Item(1)) +
        ContainerTransferPolicy::CountOf(bobPack, Item(1));
    REQUIRE(total == 1);
    REQUIRE(ContainerTransferPolicy::CountOf(alicePack, Item(1)) == 1);
    REQUIRE(ContainerTransferPolicy::CountOf(bobPack, Item(1)) == 0);
}

TEST_CASE("A repeated RequestId returns the first result without applying again", "[container_transfer]")
{
    ContainerTransferSession session;
    Inventory chest = Holding(5);
    Inventory player = Holding(0);

    bool applied = false;
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, 42, 10, true, kTake, Item(2), 5, chest, player, &applied) == kAccepted);
    REQUIRE(applied);
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, 42, 11, true, kTake, Item(2), 5, chest, player, &applied) == kAccepted);
    REQUIRE_FALSE(applied); // the replay must not be relayed again
    REQUIRE(ContainerTransferPolicy::CountOf(chest, Item(1)) == 3);
    REQUIRE(ContainerTransferPolicy::CountOf(player, Item(1)) == 2);

    // A rejected id stays rejected even if the state would now allow it.
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, 43, 11, true, kTake, Item(1), 5, chest, player) == ContainerTransferResult::kStale);
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, 43, 11, true, kTake, Item(1), 3, chest, player) == ContainerTransferResult::kStale);
    REQUIRE(ContainerTransferPolicy::CountOf(chest, Item(1)) == 3);
}

TEST_CASE("Transfers are rate limited per player per second", "[container_transfer]")
{
    ContainerTransferSession session;
    Inventory chest = Holding(1000);
    Inventory player = Holding(0);

    int64_t chestCount = 1000;
    for (uint32_t i = 0; i < ContainerTransferPolicy::kMaxTransfersPerSecond; ++i)
    {
        REQUIRE(ContainerTransferPolicy::TryTransfer(session, i + 1, 10, true, kTake, Item(1), static_cast<int32_t>(chestCount), chest, player) == kAccepted);
        --chestCount;
    }

    REQUIRE(ContainerTransferPolicy::TryTransfer(session, 1000, 10, true, kTake, Item(1), static_cast<int32_t>(chestCount), chest, player) == ContainerTransferResult::kRateLimited);
    REQUIRE(ContainerTransferPolicy::CountOf(chest, Item(1)) == chestCount);

    // The next second opens a new window.
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, 1001, 11, true, kTake, Item(1), static_cast<int32_t>(chestCount), chest, player) == kAccepted);
}

TEST_CASE("Rejected transfers leave both inventories untouched", "[container_transfer]")
{
    ContainerTransferSession session;
    Inventory chest = Holding(2);
    Inventory player = Holding(1);
    uint32_t id = 1;

    const auto unchanged = [&]
    {
        REQUIRE(ContainerTransferPolicy::CountOf(chest, Item(1)) == 2);
        REQUIRE(ContainerTransferPolicy::CountOf(player, Item(1)) == 1);
    };

    REQUIRE(ContainerTransferPolicy::TryTransfer(session, id++, 10, false, kTake, Item(1), 2, chest, player) == ContainerTransferResult::kNotAllowed);
    unchanged();
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, id++, 10, true, kTake, Item(3), 2, chest, player) == ContainerTransferResult::kInsufficient);
    unchanged();
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, id++, 10, true, kPut, Item(1), 5, chest, player) == ContainerTransferResult::kStale);
    unchanged();
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, id++, 10, true, kTake, Item(0), 2, chest, player) == ContainerTransferResult::kInvalid);
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, id++, 10, true, kTake, Item(-1), 2, chest, player) == ContainerTransferResult::kInvalid);
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, id++, 10, true, kTake, Item(1, 0), 2, chest, player) == ContainerTransferResult::kInvalid);
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, id++, 10, true, static_cast<ContainerTransferDirection>(9), Item(1), 2, chest, player) == ContainerTransferResult::kInvalid);
    unchanged();

    // Taking an item the container never had is stale (sender claims 1, server has 0).
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, id++, 10, true, kTake, Item(1, 0xBEEF), 1, chest, player) == ContainerTransferResult::kStale);
    REQUIRE(ContainerTransferPolicy::CountOf(player, Item(1, 0xBEEF)) == 0);
}

TEST_CASE("A put is checked against the container, not the owner-reported player copy", "[container_transfer]")
{
    ContainerTransferSession session;
    Inventory chest = Holding(1);
    Inventory player = Holding(1); // server copy lags behind the client, which really holds 3

    REQUIRE(ContainerTransferPolicy::TryTransfer(session, 1, 10, true, kPut, Item(3), 1, chest, player) == kAccepted);
    REQUIRE(ContainerTransferPolicy::CountOf(chest, Item(1)) == 4);
    REQUIRE(ContainerTransferPolicy::CountOf(player, Item(1)) == 0);

    // Two players putting at once: the second saw the old container count and is stale.
    ContainerTransferSession other;
    Inventory otherPack = Holding(1);
    REQUIRE(ContainerTransferPolicy::TryTransfer(other, 1, 10, true, kPut, Item(1), 1, chest, otherPack) == ContainerTransferResult::kStale);
    REQUIRE(ContainerTransferPolicy::CountOf(chest, Item(1)) == 4);
}

TEST_CASE("Only a dead NPC corpse accepts takes", "[container_transfer][corpse]")
{
    // player, dead, removal queued, direction, in range
    REQUIRE(ContainerTransferPolicy::IsCorpseTransferAllowed(false, true, false, kTake, true));

    REQUIRE_FALSE(ContainerTransferPolicy::IsCorpseTransferAllowed(false, false, false, kTake, true)); // living actor
    REQUIRE_FALSE(ContainerTransferPolicy::IsCorpseTransferAllowed(true, true, false, kTake, true));   // player corpse
    REQUIRE_FALSE(ContainerTransferPolicy::IsCorpseTransferAllowed(false, true, true, kTake, true));   // expired, being removed
    REQUIRE_FALSE(ContainerTransferPolicy::IsCorpseTransferAllowed(false, true, false, kPut, true));   // putting into a corpse
    REQUIRE_FALSE(ContainerTransferPolicy::IsCorpseTransferAllowed(false, true, false, kTake, false)); // out of range
}

TEST_CASE("A retained creature corpse's inventory is server-owned until it is queued for removal", "[container_transfer][corpse]")
{
    // player, dead, marker, removal queued
    REQUIRE(ContainerTransferPolicy::IsServerOwnedCorpse(false, true, true, false));

    REQUIRE_FALSE(ContainerTransferPolicy::IsServerOwnedCorpse(false, true, false, false)); // humanoid corpse: no marker
    REQUIRE_FALSE(ContainerTransferPolicy::IsServerOwnedCorpse(false, false, true, false)); // respawned
    REQUIRE_FALSE(ContainerTransferPolicy::IsServerOwnedCorpse(true, true, true, false));
    REQUIRE_FALSE(ContainerTransferPolicy::IsServerOwnedCorpse(false, true, true, true));
}

TEST_CASE("Two players taking the last item from a corpse: one wins and nothing is duplicated", "[container_transfer][corpse]")
{
    ContainerTransferSession alice;
    ContainerTransferSession bob;
    Inventory corpse = Holding(1);
    Inventory alicePack = Holding(0);
    Inventory bobPack = Holding(0);
    const bool allowed = ContainerTransferPolicy::IsCorpseTransferAllowed(false, true, false, kTake, true);

    REQUIRE(ContainerTransferPolicy::TryTransfer(alice, 1, 10, allowed, kTake, Item(1), 1, corpse, alicePack) == kAccepted);
    REQUIRE(ContainerTransferPolicy::TryTransfer(bob, 1, 10, allowed, kTake, Item(1), 1, corpse, bobPack) == ContainerTransferResult::kStale);

    REQUIRE(ContainerTransferPolicy::CountOf(corpse, Item(1)) == 0);
    REQUIRE(ContainerTransferPolicy::CountOf(alicePack, Item(1)) == 1);
    REQUIRE(ContainerTransferPolicy::CountOf(bobPack, Item(1)) == 0);
}

TEST_CASE("Takes from an orphaned corpse and rejections after expiry or into a corpse leave inventories untouched", "[container_transfer][corpse]")
{
    ContainerTransferSession session;
    Inventory corpse = Holding(2);
    Inventory player = Holding(1);

    // No owner is involved: the server applies the take for whoever is in range.
    REQUIRE(ContainerTransferPolicy::TryTransfer(
                session, 1, 10, ContainerTransferPolicy::IsCorpseTransferAllowed(false, true, false, kTake, true), kTake, Item(1), 2, corpse,
                player) == kAccepted);

    const bool expired = ContainerTransferPolicy::IsCorpseTransferAllowed(false, true, true, kTake, true);
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, 2, 10, expired, kTake, Item(1), 1, corpse, player) == ContainerTransferResult::kNotAllowed);

    const bool put = ContainerTransferPolicy::IsCorpseTransferAllowed(false, true, false, kPut, true);
    REQUIRE(ContainerTransferPolicy::TryTransfer(session, 3, 10, put, kPut, Item(1), 1, corpse, player) == ContainerTransferResult::kNotAllowed);

    REQUIRE(ContainerTransferPolicy::CountOf(corpse, Item(1)) == 1);
    REQUIRE(ContainerTransferPolicy::CountOf(player, Item(1)) == 2);
}

TEST_CASE("The owner's broadcast seeds a retained corpse until the first take", "[container_transfer][corpse]")
{
    // player, dead, marker, removal queued, seed closed
    // Death items (pelts, DeathItem lists) reach the server after the corpse is marked: still accepted.
    REQUIRE_FALSE(ContainerTransferPolicy::IgnoresOwnerInventoryBroadcast(false, true, true, false, false));
    // After someone took from it the server copy is authoritative.
    REQUIRE(ContainerTransferPolicy::IgnoresOwnerInventoryBroadcast(false, true, true, false, true));

    // Not a server-owned corpse: the broadcast is handled as before.
    REQUIRE_FALSE(ContainerTransferPolicy::IgnoresOwnerInventoryBroadcast(false, true, false, false, true)); // humanoid corpse
    REQUIRE_FALSE(ContainerTransferPolicy::IgnoresOwnerInventoryBroadcast(false, true, true, true, true));   // being removed
    REQUIRE_FALSE(ContainerTransferPolicy::IgnoresOwnerInventoryBroadcast(true, true, true, false, true));   // player
}
