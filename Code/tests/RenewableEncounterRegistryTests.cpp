#include <Services/RenewableEncounterRegistry.h>

#include <catch2/catch.hpp>

namespace
{
constexpr RenewableEncounterId kCave{0x0001A2B3u, 0};
constexpr RenewableEncounterId kCrypt{0x0001A2B4u, 0};
constexpr SpawnSlotId kCaveSlot{0x0010F00Du};
constexpr SpawnSlotId kCryptSlot{0x0010F00Eu};

RenewableEncounterRegistry MakeRegistry()
{
    RenewableEncounterRegistry registry;
    REQUIRE(registry.AddEncounter(kCave, RenewableEncounterPolicy{0}));
    REQUIRE(registry.AddEncounter(kCrypt, RenewableEncounterPolicy{0}));
    REQUIRE(registry.AddSlot(kCave, kCaveSlot));
    REQUIRE(registry.AddSlot(kCrypt, kCryptSlot));
    return registry;
}

std::uint64_t EpochOf(const RenewableEncounterRegistry& acRegistry, const RenewableEncounterId aId)
{
    const auto* pEncounter = acRegistry.Find(aId);
    REQUIRE(pEncounter != nullptr);
    return pEncounter->GetEpoch();
}
} // namespace

TEST_CASE("W01: registry rejects invalid and duplicate encounters and slots", "[renewable_encounter]")
{
    RenewableEncounterRegistry registry;
    REQUIRE_FALSE(registry.AddEncounter(RenewableEncounterId{}, RenewableEncounterPolicy{}));
    REQUIRE(registry.AddEncounter(kCave, RenewableEncounterPolicy{}));
    REQUIRE_FALSE(registry.AddEncounter(kCave, RenewableEncounterPolicy{}));

    REQUIRE(registry.AddSlot(kCave, kCaveSlot));
    REQUIRE_FALSE(registry.AddSlot(kCrypt, kCryptSlot)); // unknown encounter

    // A placed reference belongs to exactly one encounter.
    REQUIRE(registry.AddEncounter(kCrypt, RenewableEncounterPolicy{}));
    REQUIRE_FALSE(registry.AddSlot(kCrypt, kCaveSlot));
    REQUIRE(registry.GetEncounterCount() == 2);
}

TEST_CASE("W01: an incarnation belongs to at most one encounter", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(registry.BindIncarnation(kCave, kCaveSlot, {7, 10}, EpochOf(registry, kCave)));
    REQUIRE_FALSE(registry.BindIncarnation(kCrypt, kCryptSlot, {7, 10}, EpochOf(registry, kCrypt)));
    REQUIRE(registry.FindEncounter({7, 10}) == kCave);
    REQUIRE_FALSE(registry.FindEncounter({7, 11}));
}

TEST_CASE("W02: registry routes verified deaths to the owning encounter", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(registry.BindIncarnation(kCave, kCaveSlot, {7, 10}, EpochOf(registry, kCave)));
    REQUIRE(registry.BindIncarnation(kCrypt, kCryptSlot, {8, 11}, EpochOf(registry, kCrypt)));

    REQUIRE(registry.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(registry.Find(kCave)->IsCleared());
    REQUIRE_FALSE(registry.Find(kCrypt)->IsCleared());

    REQUIRE(registry.RecordVerifiedDeath({99, 99}, 50) == RenewableEncounterState::DeathResult::UnknownIncarnation);
}

TEST_CASE("W02: registry forgets incarnations on release and reset", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(registry.BindIncarnation(kCrypt, kCryptSlot, {8, 11}, EpochOf(registry, kCrypt)));
    REQUIRE(registry.ReleaseIncarnation({8, 11}) == RenewableEncounterState::ReleaseResult::Released);
    REQUIRE_FALSE(registry.FindEncounter({8, 11}));

    REQUIRE(registry.BindIncarnation(kCave, kCaveSlot, {7, 10}, EpochOf(registry, kCave)));
    REQUIRE(registry.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(registry.TryReset(kCave, 50));
    REQUIRE_FALSE(registry.FindEncounter({7, 10}));

    // W06: a freed identity is retired, so it cannot appear again anywhere;
    // only a fresh incarnation can fill the slot.
    REQUIRE_FALSE(registry.BindIncarnation(kCrypt, kCryptSlot, {7, 10}, EpochOf(registry, kCrypt)));
    REQUIRE(registry.BindIncarnation(kCrypt, kCryptSlot, {7, 12}, EpochOf(registry, kCrypt)));
    REQUIRE(registry.FindEncounter({7, 12}) == kCrypt);
    REQUIRE_FALSE(registry.TryReset(RenewableEncounterId{0x9999u, 0}, 50));
}

TEST_CASE("W01: registry resolves the encounter owning a placed reference", "[renewable_encounter]")
{
    const auto registry = MakeRegistry();
    REQUIRE(registry.FindEncounter(kCaveSlot) == kCave);
    REQUIRE(registry.FindEncounter(kCryptSlot) == kCrypt);
    REQUIRE_FALSE(registry.FindEncounter(SpawnSlotId{0x0010F00Fu}));
}

TEST_CASE("W02: registry keeps slot ownership when a cleared encounter rejects a slot", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(registry.BindIncarnation(kCave, kCaveSlot, {7, 10}, EpochOf(registry, kCave)));
    REQUIRE(registry.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::Recorded);

    constexpr SpawnSlotId kLateSlot{0x0010F00Fu};
    REQUIRE_FALSE(registry.AddSlot(kCave, kLateSlot));
    REQUIRE_FALSE(registry.FindEncounter(kLateSlot));
    REQUIRE(registry.AddSlot(kCrypt, kLateSlot)); // the slot is still free for another encounter
}
