#include <Services/RenewableEncounterRegistry.h>

#include <catch2/catch.hpp>

namespace
{
constexpr RenewableEncounterId kCave{0x0001A2B3u, 0};
constexpr RenewableEncounterId kCrypt{0x0001A2B4u, 0};
constexpr SpawnSlotId kSlotA{0x0010F00Du};
constexpr SpawnSlotId kSlotB{0x0010F00Eu};
constexpr SpawnSlotId kCryptSlot{0x0010F00Fu};

using Status = RenewableEncounterRegistry::IncarnationStatus;
using Death = RenewableEncounterState::DeathResult;
using Release = RenewableEncounterState::ReleaseResult;

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

void ClearAndReset(RenewableEncounterRegistry& aRegistry, const EncounterIncarnation aFirst, const EncounterIncarnation aSecond)
{
    const auto epoch = aRegistry.Find(kCave)->GetEpoch();
    REQUIRE(aRegistry.BindIncarnation(kCave, kSlotA, aFirst, epoch));
    REQUIRE(aRegistry.BindIncarnation(kCave, kSlotB, aSecond, epoch));
    REQUIRE(aRegistry.RecordVerifiedDeath(aFirst, 10) == Death::Recorded);
    REQUIRE(aRegistry.RecordVerifiedDeath(aSecond, 11) == Death::Recorded);
    REQUIRE(aRegistry.TryReset(kCave, 20));
}
} // namespace

TEST_CASE("W06: incarnations from before a reset are reported as stale, not unknown", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    const EncounterIncarnation first{7, 10};
    const EncounterIncarnation second{8, 10};
    REQUIRE(registry.GetIncarnationStatus(first) == Status::Unknown);

    ClearAndReset(registry, first, second);

    REQUIRE(registry.GetIncarnationStatus(first) == Status::Stale);
    REQUIRE(registry.GetIncarnationStatus(second) == Status::Stale);
    REQUIRE(registry.GetIncarnationStatus({99, 1}) == Status::Unknown);

    const auto retired = registry.FindRetired(first);
    REQUIRE(retired.has_value());
    REQUIRE(retired->Encounter == kCave);
    REQUIRE(retired->Epoch == 0);
}

TEST_CASE("W06: late packets of a stale incarnation are rejected as stale", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    const EncounterIncarnation first{7, 10};
    const EncounterIncarnation second{8, 10};
    ClearAndReset(registry, first, second);

    REQUIRE(registry.RecordVerifiedDeath(first, 30) == Death::StaleIncarnation);
    REQUIRE(registry.ReleaseIncarnation(second) == Release::StaleIncarnation);
    REQUIRE(registry.RecordVerifiedDeath({99, 1}, 30) == Death::UnknownIncarnation);
    REQUIRE_FALSE(registry.Find(kCave)->IsCleared());
}

TEST_CASE("W06: a stale incarnation can never be bound again", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    const EncounterIncarnation first{7, 10};
    const EncounterIncarnation second{8, 10};
    ClearAndReset(registry, first, second);

    const auto epoch = registry.Find(kCave)->GetEpoch();
    REQUIRE(epoch == 1);
    // Not in the same encounter, not in a different one.
    REQUIRE_FALSE(registry.BindIncarnation(kCave, kSlotA, first, epoch));
    REQUIRE_FALSE(registry.BindIncarnation(kCrypt, kCryptSlot, second, registry.Find(kCrypt)->GetEpoch()));
    REQUIRE(registry.Find(kCave)->GetSlotStatus(kSlotA) == RenewableEncounterState::SlotStatus::Unbound);

    // A fresh incarnation (same server id, new lifecycle generation) is fine.
    const EncounterIncarnation fresh{7, 11};
    REQUIRE(registry.BindIncarnation(kCave, kSlotA, fresh, epoch));
    REQUIRE(registry.GetIncarnationStatus(fresh) == Status::Current);
}

TEST_CASE("W06: a released incarnation is retired so it cannot come back", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    const EncounterIncarnation actor{7, 10};
    REQUIRE(registry.BindIncarnation(kCave, kSlotA, actor, 0));
    REQUIRE(registry.ReleaseIncarnation(actor) == Release::Released);

    REQUIRE(registry.GetIncarnationStatus(actor) == Status::Stale);
    REQUIRE_FALSE(registry.BindIncarnation(kCave, kSlotA, actor, 0));
    REQUIRE(registry.RecordVerifiedDeath(actor, 5) == Death::StaleIncarnation);
    REQUIRE(registry.BindIncarnation(kCave, kSlotA, {7, 11}, 0));
}

TEST_CASE("W06: reset issues spawn requests for the new epoch", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    auto requests = registry.GetSpawnRequests(kCave);
    REQUIRE(requests.size() == 2);
    REQUIRE(requests[0].Slot == kSlotA);
    REQUIRE(requests[0].Epoch == 0);

    ClearAndReset(registry, {7, 10}, {8, 10});

    requests = registry.GetSpawnRequests(kCave);
    REQUIRE(requests.size() == 2);
    for (const auto& request : requests)
        REQUIRE(request.Epoch == 1);

    // Binding one slot removes it from the pending list.
    REQUIRE(registry.BindIncarnation(kCave, kSlotA, {7, 11}, 1));
    requests = registry.GetSpawnRequests(kCave);
    REQUIRE(requests.size() == 1);
    REQUIRE(requests[0].Slot == kSlotB);

    REQUIRE(registry.GetSpawnRequests({0x0BADu, 0}).empty());
}

TEST_CASE("W06: a spawn request from the previous epoch cannot fill the new cycle", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    const auto oldRequest = registry.GetSpawnRequests(kCave).front();
    ClearAndReset(registry, {7, 10}, {8, 10});

    REQUIRE_FALSE(registry.BindIncarnation(kCave, oldRequest.Slot, {7, 11}, oldRequest.Epoch));
    REQUIRE(registry.GetIncarnationStatus({7, 11}) == Status::Unknown);
}

TEST_CASE("W06: a cleared encounter issues no spawn requests", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(registry.BindIncarnation(kCrypt, kCryptSlot, {9, 10}, 0));
    REQUIRE(registry.RecordVerifiedDeath({9, 10}, 10) == Death::Recorded);
    REQUIRE(registry.GetSpawnRequests(kCrypt).empty());
}
