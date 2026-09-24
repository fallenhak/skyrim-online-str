#include <Services/RenewableEncounterRegistry.h>

#include <catch2/catch.hpp>

namespace
{
constexpr RenewableEncounterId kCave{0x0001A2B3u, 0};
constexpr RenewableEncounterId kCrypt{0x0001A2B4u, 0};
constexpr SpawnSlotId kSlotA{0x0010F00Du};
constexpr SpawnSlotId kSlotB{0x0010F00Eu};
constexpr SpawnSlotId kCryptSlot{0x0010F00Fu};
constexpr std::uint32_t kAlice = 1;
constexpr std::uint32_t kBob = 2;

using Status = RenewableEncounterRegistry::IncarnationStatus;
using Death = RenewableEncounterState::DeathResult;

RenewableEncounterRegistry MakeRegistry()
{
    RenewableEncounterRegistry registry;
    REQUIRE(registry.AddEncounter(kCave, RenewableEncounterPolicy{0}));
    REQUIRE(registry.AddEncounter(kCrypt, RenewableEncounterPolicy{0}));
    REQUIRE(registry.AddSlot(kCave, kSlotA));
    REQUIRE(registry.AddSlot(kCave, kSlotB));
    REQUIRE(registry.AddSlot(kCrypt, kCryptSlot));
    return registry;
}
} // namespace

TEST_CASE("W07: only one player can claim a slot's spawn at a time", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    const auto alice = registry.ClaimSpawn(kCave, kSlotA, 0, kAlice, 100);
    REQUIRE(alice.has_value());
    REQUIRE(alice->Owner == kAlice);
    REQUIRE_FALSE(registry.ClaimSpawn(kCave, kSlotA, 0, kBob, 100));
    REQUIRE_FALSE(registry.ClaimSpawn(kCave, kSlotA, 0, kAlice, 100)); // no double claim either

    // A claimed slot is no longer requested, other slots still are.
    const auto requests = registry.GetSpawnRequests(kCave);
    REQUIRE(requests.size() == 1);
    REQUIRE(requests[0].Slot == kSlotB);

    REQUIRE(registry.CompleteSpawn(*alice, {7, 10}));
    REQUIRE(registry.GetIncarnationStatus({7, 10}) == Status::Current);
    REQUIRE_FALSE(registry.ClaimSpawn(kCave, kSlotA, 0, kBob, 101)); // slot is alive now
}

TEST_CASE("W07: a ticket completes once", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    const auto ticket = registry.ClaimSpawn(kCave, kSlotA, 0, kAlice, 100);
    REQUIRE(ticket.has_value());
    REQUIRE(registry.CompleteSpawn(*ticket, {7, 10}));
    REQUIRE_FALSE(registry.CompleteSpawn(*ticket, {7, 11}));
    REQUIRE(registry.GetIncarnationStatus({7, 11}) == Status::Unknown);
}

TEST_CASE("W07: a claimed slot cannot be filled around its ticket", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(registry.ClaimSpawn(kCave, kSlotA, 0, kAlice, 100));
    REQUIRE_FALSE(registry.BindIncarnation(kCave, kSlotA, {8, 10}, 0));
}

TEST_CASE("W07: claims carry the current epoch only", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE_FALSE(registry.ClaimSpawn(kCave, kSlotA, 1, kAlice, 100));
    REQUIRE_FALSE(registry.ClaimSpawn({0x0BADu, 0}, kSlotA, 0, kAlice, 100));
    REQUIRE_FALSE(registry.ClaimSpawn(kCave, kCryptSlot, 0, kAlice, 100)); // slot of another encounter
    REQUIRE_FALSE(registry.ClaimSpawn(kCave, kSlotA, 0, 0, 100));          // invalid player
}

TEST_CASE("W07: a disconnect frees the player's claims and voids their tickets", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    const auto alice = registry.ClaimSpawn(kCave, kSlotA, 0, kAlice, 100);
    const auto aliceCrypt = registry.ClaimSpawn(kCrypt, kCryptSlot, 0, kAlice, 100);
    const auto bob = registry.ClaimSpawn(kCave, kSlotB, 0, kBob, 100);
    REQUIRE((alice && aliceCrypt && bob));

    REQUIRE(registry.ReleasePlayerClaims(kAlice) == 2);
    REQUIRE(registry.GetSpawnRequests(kCave).size() == 1); // slot A requestable again

    // The new owner (Bob) takes over; Alice's late completion after reconnect is refused.
    const auto takeover = registry.ClaimSpawn(kCave, kSlotA, 0, kBob, 101);
    REQUIRE(takeover.has_value());
    REQUIRE_FALSE(registry.CompleteSpawn(*alice, {7, 10}));
    REQUIRE(registry.CompleteSpawn(*takeover, {8, 10}));
    REQUIRE(registry.CompleteSpawn(*bob, {9, 10}));
    REQUIRE(registry.Find(kCave)->GetMembership().Alive == 2);
}

TEST_CASE("W07: expired claims are reclaimed", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    const auto stuck = registry.ClaimSpawn(kCave, kSlotA, 0, kAlice, 100);
    REQUIRE(stuck.has_value());

    REQUIRE(registry.ExpireSpawnClaims(149, 50) == 0); // claimed at 100, ttl 50: alive until 150
    REQUIRE(registry.ExpireSpawnClaims(150, 50) == 1);
    REQUIRE_FALSE(registry.CompleteSpawn(*stuck, {7, 10}));

    const auto retry = registry.ClaimSpawn(kCave, kSlotA, 0, kBob, 150);
    REQUIRE(retry.has_value());
    REQUIRE(retry->Id != stuck->Id);
    REQUIRE(registry.CompleteSpawn(*retry, {8, 10}));
}

TEST_CASE("W07: a reset voids outstanding claims of that encounter only", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(registry.BindIncarnation(kCave, kSlotA, {7, 10}, 0));
    REQUIRE(registry.BindIncarnation(kCave, kSlotB, {8, 10}, 0));
    REQUIRE(registry.RecordVerifiedDeath({7, 10}, 10) == Death::Recorded);
    REQUIRE(registry.RecordVerifiedDeath({8, 10}, 10) == Death::Recorded);
    const auto crypt = registry.ClaimSpawn(kCrypt, kCryptSlot, 0, kAlice, 10);
    REQUIRE(crypt.has_value());

    REQUIRE(registry.TryReset(kCave, 20));
    REQUIRE(registry.GetSpawnRequests(kCave).size() == 2);
    REQUIRE(registry.CompleteSpawn(*crypt, {9, 10}));
}

TEST_CASE("W07: restart restores epoch and cleared state, and old tickets are stale", "[renewable_encounter]")
{
    auto before = MakeRegistry();
    REQUIRE(before.BindIncarnation(kCrypt, kCryptSlot, {9, 10}, 0));
    REQUIRE(before.RecordVerifiedDeath({9, 10}, 40) == Death::Recorded);
    const auto preRestart = before.ClaimSpawn(kCave, kSlotA, 0, kAlice, 40);
    REQUIRE(preRestart.has_value());

    const auto snapshot = before.Snapshot(40);
    REQUIRE(snapshot.size() == 2);

    auto after = MakeRegistry(); // reloaded from server configuration
    REQUIRE(after.Restore(snapshot, 0));

    // Killed creatures stay dead across the restart.
    const auto* pCrypt = after.Find(kCrypt);
    REQUIRE(pCrypt->IsCleared());
    REQUIRE(pCrypt->GetMembership().Dead == 1);
    REQUIRE(after.GetSpawnRequests(kCrypt).empty());
    REQUIRE(after.TryReset(kCrypt, 0));
    REQUIRE(after.GetSpawnRequests(kCrypt).size() == 1);

    // The live encounter starts a new epoch, so anything requested before the restart is stale.
    REQUIRE(after.Find(kCave)->GetEpoch() == 1);
    REQUIRE_FALSE(after.CompleteSpawn(*preRestart, {7, 10}));
    REQUIRE_FALSE(after.BindIncarnation(kCave, kSlotA, {7, 10}, 0));
    REQUIRE(after.GetSpawnRequests(kCave).size() == 2);
    REQUIRE(after.GetSpawnRequests(kCave)[0].Epoch == 1);
}

TEST_CASE("W08: the remaining cooldown survives a restart whose tick counter starts over", "[renewable_encounter]")
{
    const auto make = [] {
        RenewableEncounterRegistry registry;
        REQUIRE(registry.AddEncounter(kCrypt, RenewableEncounterPolicy{100}));
        REQUIRE(registry.AddSlot(kCrypt, kCryptSlot));
        return registry;
    };

    auto before = make();
    REQUIRE(before.BindIncarnation(kCrypt, kCryptSlot, {9, 10}, 0));
    REQUIRE(before.RecordVerifiedDeath({9, 10}, 40000) == Death::Recorded);
    REQUIRE(before.Find(kCrypt)->GetResetCooldownRemaining(40030) == 70);

    // Only the minimum survives: epoch, cleared, remaining cooldown. No absolute tick.
    const auto snapshot = before.Snapshot(40030);
    REQUIRE(snapshot.size() == 1);
    REQUIRE(snapshot[0].Epoch == 0);
    REQUIRE(snapshot[0].Cleared);
    REQUIRE(snapshot[0].CooldownRemainingTicks == 70);

    auto after = make(); // new process: ticks start near zero
    REQUIRE(after.Restore(snapshot, 5));
    REQUIRE(after.Find(kCrypt)->GetResetCooldownRemaining(5) == 70);
    REQUIRE_FALSE(after.TryReset(kCrypt, 74));
    REQUIRE(after.TryReset(kCrypt, 75));
}

TEST_CASE("W08: an elapsed cooldown stays elapsed and a live encounter carries none", "[renewable_encounter]")
{
    auto before = MakeRegistry();
    REQUIRE(before.BindIncarnation(kCrypt, kCryptSlot, {9, 10}, 0));
    REQUIRE(before.RecordVerifiedDeath({9, 10}, 10) == Death::Recorded);

    const auto snapshot = before.Snapshot(500);
    for (const auto& entry : snapshot)
    {
        REQUIRE(entry.CooldownRemainingTicks == 0);
        REQUIRE(entry.Cleared == (entry.Id == kCrypt));
    }

    auto after = MakeRegistry();
    REQUIRE(after.Restore(snapshot, 0));
    REQUIRE(after.Find(kCrypt)->IsResetEligible(0));
    REQUIRE(after.Find(kCave)->GetResetCooldownRemaining(0) == 0);
}

TEST_CASE("W07: restore only applies to a freshly configured registry", "[renewable_encounter]")
{
    auto before = MakeRegistry();
    const auto snapshot = before.Snapshot(0);

    auto busy = MakeRegistry();
    REQUIRE(busy.BindIncarnation(kCave, kSlotA, {7, 10}, 0));
    REQUIRE_FALSE(busy.Restore(snapshot, 0));

    RenewableEncounterRegistry unconfigured;
    REQUIRE_FALSE(unconfigured.Restore(snapshot, 0)); // snapshot names encounters the config lacks
}
