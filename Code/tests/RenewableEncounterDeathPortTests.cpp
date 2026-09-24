#include <Services/RenewableEncounterDeathPort.h>

#include <catch2/catch.hpp>

namespace
{
constexpr RenewableEncounterId kCave{0x0001A2B3u, 0};
constexpr SpawnSlotId kSlotA{0x0010F00Du};
constexpr SpawnSlotId kSlotB{0x0010F00Eu};

using Death = RenewableEncounterState::DeathResult;

RenewableEncounterRegistry MakeRegistry()
{
    RenewableEncounterRegistry registry;
    REQUIRE(registry.AddEncounter(kCave, RenewableEncounterPolicy{0}));
    REQUIRE(registry.AddSlot(kCave, kSlotA));
    REQUIRE(registry.AddSlot(kCave, kSlotB));
    return registry;
}
} // namespace

TEST_CASE("W03: the canonical death event maps to the same incarnation identity", "[renewable_encounter]")
{
    const AcceptedCanonicalCreatureDeathEvent event{0xDEADBEEFu, 0x1234567890ABCDEFull};
    const auto incarnation = ToEncounterIncarnation(event);
    REQUIRE(incarnation.ServerId == 0xDEADBEEFu);
    REQUIRE(incarnation.LifecycleGeneration == 0x1234567890ABCDEFull);
}

TEST_CASE("W03: only combat-accepted deaths clear an encounter", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(registry.BindIncarnation(kCave, kSlotA, {7, 10}, 0));
    REQUIRE(registry.BindIncarnation(kCave, kSlotB, {8, 10}, 0));

    REQUIRE(RecordCanonicalCreatureDeath(registry, AcceptedCanonicalCreatureDeathEvent{7, 10}, 5) == Death::Recorded);
    REQUIRE_FALSE(registry.Find(kCave)->IsCleared());
    REQUIRE(RecordCanonicalCreatureDeath(registry, AcceptedCanonicalCreatureDeathEvent{8, 10}, 6) == Death::Recorded);
    REQUIRE(registry.Find(kCave)->IsCleared());
    REQUIRE(registry.Find(kCave)->GetClearedTick() == std::optional<std::uint64_t>{6});
}

TEST_CASE("W03: a repeated death event changes nothing", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(registry.BindIncarnation(kCave, kSlotA, {7, 10}, 0));
    REQUIRE(RecordCanonicalCreatureDeath(registry, AcceptedCanonicalCreatureDeathEvent{7, 10}, 5) == Death::Recorded);
    REQUIRE(RecordCanonicalCreatureDeath(registry, AcceptedCanonicalCreatureDeathEvent{7, 10}, 9) == Death::AlreadyDead);
}

TEST_CASE("W03: deaths of creatures outside any encounter are ignored", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(RecordCanonicalCreatureDeath(registry, AcceptedCanonicalCreatureDeathEvent{99, 1}, 5) == Death::UnknownIncarnation);
    // Same server id, other lifecycle generation: a different creature (EnTT id reuse).
    REQUIRE(registry.BindIncarnation(kCave, kSlotA, {7, 10}, 0));
    REQUIRE(RecordCanonicalCreatureDeath(registry, AcceptedCanonicalCreatureDeathEvent{7, 11}, 5) == Death::UnknownIncarnation);
    REQUIRE(registry.Find(kCave)->GetSlotStatus(kSlotA) == RenewableEncounterState::SlotStatus::Alive);
}

TEST_CASE("W03: a death event arriving after the reset is stale", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(registry.BindIncarnation(kCave, kSlotA, {7, 10}, 0));
    REQUIRE(registry.BindIncarnation(kCave, kSlotB, {8, 10}, 0));
    REQUIRE(RecordCanonicalCreatureDeath(registry, AcceptedCanonicalCreatureDeathEvent{7, 10}, 5) == Death::Recorded);
    REQUIRE(RecordCanonicalCreatureDeath(registry, AcceptedCanonicalCreatureDeathEvent{8, 10}, 5) == Death::Recorded);
    REQUIRE(registry.TryReset(kCave, 10));

    REQUIRE(registry.BindIncarnation(kCave, kSlotA, {7, 12}, 1));
    REQUIRE(RecordCanonicalCreatureDeath(registry, AcceptedCanonicalCreatureDeathEvent{8, 10}, 11) == Death::StaleIncarnation);
    REQUIRE(registry.Find(kCave)->GetSlotStatus(kSlotA) == RenewableEncounterState::SlotStatus::Alive);
}
