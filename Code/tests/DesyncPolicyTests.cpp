#include <TiltedCore/Stl.hpp>

#include <Services/DesyncPolicy.h>

#include <catch2/catch.hpp>

namespace
{
ObjectStateDigest MakeDigest(uint8_t aFlags, uint8_t aLockLevel = 0)
{
    ObjectStateDigest digest;
    digest.Id = GameId{0, 0xB9BBA};
    digest.CellId = GameId{0, 0x1234};
    digest.StateFlags = aFlags;
    digest.LockLevel = aLockLevel;
    return digest;
}
} // namespace

TEST_CASE("Desync compare ignores fields the server does not own", "[desync]")
{
    DesyncPolicy::ServerView server{};
    // Untrusted, unknown door, not harvestable: nothing to compare.
    server.IsDoor = true;
    REQUIRE(DesyncPolicy::Compare(server, MakeDigest(ObjectStateDigest::kLocked | ObjectStateDigest::kDoorOpen | ObjectStateDigest::kDisabled, 25)).empty());
}

TEST_CASE("Desync compare reports taken, harvested, door and lock differences", "[desync]")
{
    DesyncPolicy::ServerView loot{};
    loot.IsOpenLoot = true;
    loot.IsLootTaken = true;
    auto result = DesyncPolicy::Compare(loot, MakeDigest(0));
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].Kind == DesyncPolicy::Field::kLootTaken);
    REQUIRE(DesyncPolicy::Compare(loot, MakeDigest(ObjectStateDigest::kDisabled)).empty());

    DesyncPolicy::ServerView flora{};
    flora.IsHarvestable = true;
    result = DesyncPolicy::Compare(flora, MakeDigest(ObjectStateDigest::kDisabled));
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].Kind == DesyncPolicy::Field::kHarvested);

    DesyncPolicy::ServerView door{};
    door.IsDoor = true;
    door.IsDoorStateKnown = true;
    door.IsDoorOpen = true;
    result = DesyncPolicy::Compare(door, MakeDigest(0));
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].Server == "open");
    REQUIRE(result[0].Client == "closed");

    DesyncPolicy::ServerView chest{};
    chest.HasTrustedState = true;
    chest.IsLocked = true;
    chest.LockLevel = 50;
    REQUIRE(DesyncPolicy::Compare(chest, MakeDigest(ObjectStateDigest::kLocked, 50)).empty());
    result = DesyncPolicy::Compare(chest, MakeDigest(0, 50));
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].Kind == DesyncPolicy::Field::kLock);
    REQUIRE(result[0].Server == "locked(50)");
    REQUIRE(result[0].Client == "unlocked");
}

TEST_CASE("Desync compare shows both container contents", "[desync]")
{
    // The sixth test's chest: 7 gold on the server, 27 on one client.
    DesyncPolicy::ServerView chest{};
    chest.HasTrustedState = true;
    chest.IsContainer = true;
    chest.Items = {{GameId{0, 0xF}, 7}};

    auto digest = MakeDigest(ObjectStateDigest::kHasInventory);
    digest.Items = {{GameId{0, 0xF}, 7}};
    REQUIRE(DesyncPolicy::Compare(chest, digest).empty());

    digest.Items = {{GameId{0, 0xF}, 27}, {GameId{0, 0xA44AE}, 1}};
    const auto result = DesyncPolicy::Compare(chest, digest);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].Kind == DesyncPolicy::Field::kInventory);
    REQUIRE(result[0].Server == "{0:F x7}");
    REQUIRE(result[0].Client == "{0:F x27, 0:A44AE x1}");

    // No inventory in the digest: not compared.
    REQUIRE(DesyncPolicy::Compare(chest, MakeDigest(0)).empty());
}

TEST_CASE("Desync tracker logs a mismatch once it persists and when it ends", "[desync]")
{
    using Tracker = DesyncPolicy::Tracker;
    Tracker tracker;
    const Tracker::Key key{1, GameId{0, 0x99535}, DesyncPolicy::Field::kDoor};

    REQUIRE(tracker.Observe(key, "") == Tracker::Event::kNone);
    REQUIRE(tracker.Size() == 0);

    // A single report can land mid-swing: not logged.
    REQUIRE(tracker.Observe(key, "open | closed") == Tracker::Event::kNone);
    REQUIRE(tracker.Observe(key, "") == Tracker::Event::kNone);

    REQUIRE(tracker.Observe(key, "open | closed") == Tracker::Event::kNone);
    REQUIRE(tracker.Observe(key, "open | closed") == Tracker::Event::kNew);
    REQUIRE(tracker.Observe(key, "open | closed") == Tracker::Event::kNone);

    // A different value is a new mismatch.
    REQUIRE(tracker.Observe(key, "closed | open") == Tracker::Event::kNone);
    REQUIRE(tracker.Observe(key, "closed | open") == Tracker::Event::kNew);

    REQUIRE(tracker.Observe(key, "") == Tracker::Event::kResolved);
    REQUIRE(tracker.Size() == 0);

    REQUIRE(tracker.Observe(key, "open | closed") == Tracker::Event::kNone);
    tracker.ForgetPlayer(2);
    REQUIRE(tracker.Size() == 1);
    tracker.ForgetPlayer(1);
    REQUIRE(tracker.Size() == 0);
}
