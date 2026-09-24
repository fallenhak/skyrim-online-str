#include <Services/RenewableEncounterSnapshotMapping.h>

#include <catch2/catch.hpp>

namespace
{
constexpr RenewableEncounterId kCave{0x0001A2B3u, 0};
constexpr RenewableEncounterId kCrypt{0x0001A2B4u, 1};
constexpr RenewableEncounterId kRemoved{0x0001A2B5u, 0};
constexpr SpawnSlotId kSlotA{0x0010F00Du};

RenewableEncounterRegistry MakeRegistry()
{
    RenewableEncounterRegistry registry;
    REQUIRE(registry.AddEncounter(kCave, RenewableEncounterPolicy{100}));
    REQUIRE(registry.AddEncounter(kCrypt, RenewableEncounterPolicy{100}));
    REQUIRE(registry.AddSlot(kCave, kSlotA));
    return registry;
}
} // namespace

TEST_CASE("W11: snapshot entries map to records field by field", "[renewable_encounter]")
{
    const std::vector<RenewableEncounterRegistry::EncounterSnapshot> snapshot{{kCave, 3, true, 40}, {kCrypt, 0, false, 0}};
    const auto records = ToRenewableEncounterRecords(snapshot);

    REQUIRE(records.size() == 2);
    REQUIRE(records[0].CellFormId == kCave.CellFormId);
    REQUIRE(records[0].GroupIndex == kCave.GroupIndex);
    REQUIRE(records[0].Epoch == 3);
    REQUIRE(records[0].Cleared);
    REQUIRE(records[0].CooldownRemainingTicks == 40);
    REQUIRE(records[1].CellFormId == kCrypt.CellFormId);
    REQUIRE(records[1].GroupIndex == kCrypt.GroupIndex);
    REQUIRE_FALSE(records[1].Cleared);
}

TEST_CASE("W11: records of encounters no longer configured are skipped, not fatal", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    const std::vector<Persistence::RenewableEncounterRecord> records{
        {kCave.CellFormId, kCave.GroupIndex, 2, true, 50},
        {kRemoved.CellFormId, kRemoved.GroupIndex, 7, false, 0},
    };

    const auto restorable = ToRestorableSnapshot(registry, records);
    REQUIRE(restorable.Skipped == 1);
    REQUIRE(restorable.Entries.size() == 1);
    REQUIRE(restorable.Entries[0].Id == kCave);

    // Without the filter Restore would reject the whole snapshot because of kRemoved.
    REQUIRE(registry.Restore(restorable.Entries, 1000));
    REQUIRE(registry.Find(kCave)->GetEpoch() == 3);
    REQUIRE(registry.Find(kCave)->IsCleared());
    REQUIRE(registry.Find(kCave)->GetResetCooldownRemaining(1000) == 50);
    REQUIRE(registry.Find(kCrypt)->GetEpoch() == 0);
}

TEST_CASE("W11: a save/load round trip through records restores the same state", "[renewable_encounter]")
{
    auto before = MakeRegistry();
    REQUIRE(before.BindIncarnation(kCave, kSlotA, {7, 1}, 0));
    REQUIRE(before.RecordVerifiedDeath({7, 1}, 10) == RenewableEncounterState::DeathResult::Recorded);

    const auto records = ToRenewableEncounterRecords(before.Snapshot(30));

    auto after = MakeRegistry();
    const auto restorable = ToRestorableSnapshot(after, records);
    REQUIRE(restorable.Skipped == 0);
    REQUIRE(after.Restore(restorable.Entries, 5));
    REQUIRE(after.Find(kCave)->IsCleared());
    REQUIRE(after.Find(kCave)->GetResetCooldownRemaining(5) == 80);
    REQUIRE_FALSE(after.Find(kCrypt)->IsCleared());
}
