#include <Services/RenewableEncounterRegistry.h>

#include <catch2/catch.hpp>

namespace
{
constexpr std::uint32_t kCaveCell = 0x0001A2B3u;
constexpr RenewableEncounterId kCaveUpper{kCaveCell, 0};
constexpr RenewableEncounterId kCaveLower{kCaveCell, 1};
constexpr RenewableEncounterId kCrypt{0x0001A2B4u, 0};
constexpr SpawnSlotId kUpperSlot{0x0010F00Du};
constexpr SpawnSlotId kLowerSlot{0x0010F00Eu};
constexpr SpawnSlotId kCryptSlot{0x0010F00Fu};
constexpr std::uint32_t kAlice = 1;
constexpr std::uint32_t kBob = 2;

using Blocker = RenewableEncounterRegistry::ResetBlocker;

RenewableEncounterRegistry MakeRegistry()
{
    RenewableEncounterRegistry registry;
    REQUIRE(registry.AddEncounter(kCaveUpper, RenewableEncounterPolicy{0}));
    REQUIRE(registry.AddEncounter(kCaveLower, RenewableEncounterPolicy{0}));
    REQUIRE(registry.AddEncounter(kCrypt, RenewableEncounterPolicy{0}));
    REQUIRE(registry.AddSlot(kCaveUpper, kUpperSlot));
    REQUIRE(registry.AddSlot(kCaveLower, kLowerSlot));
    REQUIRE(registry.AddSlot(kCrypt, kCryptSlot));
    return registry;
}

void Clear(RenewableEncounterRegistry& aRegistry, const RenewableEncounterId aId, const SpawnSlotId aSlot, const EncounterIncarnation aIncarnation)
{
    REQUIRE(aRegistry.BindIncarnation(aId, aSlot, aIncarnation, aRegistry.Find(aId)->GetEpoch()));
    REQUIRE(aRegistry.RecordVerifiedDeath(aIncarnation, 10) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(aRegistry.Find(aId)->IsCleared());
}
} // namespace

TEST_CASE("W05: a player in the encounter cell blocks its reset", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    Clear(registry, kCaveUpper, kUpperSlot, {7, 10});

    REQUIRE(registry.SetPlayerCell(kAlice, kCaveCell));
    REQUIRE(registry.IsOccupied(kCaveUpper));
    REQUIRE(registry.GetResetBlocker(kCaveUpper, 20) == Blocker::Occupied);
    REQUIRE_FALSE(registry.TryReset(kCaveUpper, 20));
    REQUIRE(registry.Find(kCaveUpper)->IsCleared()); // nothing changed

    REQUIRE(registry.SetPlayerCell(kAlice, 0)); // left for an untracked cell
    REQUIRE_FALSE(registry.IsOccupied(kCaveUpper));
    REQUIRE(registry.GetResetBlocker(kCaveUpper, 20) == Blocker::None);
    REQUIRE(registry.TryReset(kCaveUpper, 20));
}

TEST_CASE("W05: occupancy is per cell, so it covers every group in that cell", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    Clear(registry, kCaveLower, kLowerSlot, {8, 11});
    Clear(registry, kCrypt, kCryptSlot, {9, 12});

    REQUIRE(registry.SetPlayerCell(kAlice, kCaveCell));
    REQUIRE(registry.IsOccupied(kCaveUpper));
    REQUIRE(registry.IsOccupied(kCaveLower));
    REQUIRE_FALSE(registry.TryReset(kCaveLower, 20));

    REQUIRE_FALSE(registry.IsOccupied(kCrypt));
    REQUIRE(registry.TryReset(kCrypt, 20));
}

TEST_CASE("W05: the encounter stays occupied until its last player leaves", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    Clear(registry, kCaveUpper, kUpperSlot, {7, 10});

    REQUIRE(registry.SetPlayerCell(kAlice, kCaveCell));
    REQUIRE(registry.SetPlayerCell(kBob, kCaveCell));
    REQUIRE(registry.SetPlayerCell(kAlice, kCaveCell)); // repeated update is idempotent
    REQUIRE(registry.GetOccupantCount(kCaveUpper) == 2);

    REQUIRE(registry.SetPlayerCell(kAlice, kCrypt.CellFormId)); // moving leaves the old cell
    REQUIRE(registry.GetOccupantCount(kCaveUpper) == 1);
    REQUIRE(registry.GetOccupantCount(kCrypt) == 1);
    REQUIRE_FALSE(registry.TryReset(kCaveUpper, 20));

    registry.RemovePlayer(kBob); // disconnect
    REQUIRE(registry.GetOccupantCount(kCaveUpper) == 0);
    REQUIRE(registry.TryReset(kCaveUpper, 20));
}

TEST_CASE("W05: reset blockers are reported in a stable order", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(registry.GetResetBlocker(RenewableEncounterId{0x9999u, 0}, 20) == Blocker::UnknownEncounter);

    // Not cleared yet: eligibility is reported before occupancy.
    REQUIRE(registry.SetPlayerCell(kAlice, kCaveCell));
    REQUIRE(registry.GetResetBlocker(kCaveUpper, 20) == Blocker::NotEligible);

    Clear(registry, kCaveUpper, kUpperSlot, {7, 10});
    REQUIRE(registry.GetResetBlocker(kCaveUpper, 20) == Blocker::Occupied);
}

TEST_CASE("W05: invalid players are rejected and unknown players are harmless", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE_FALSE(registry.SetPlayerCell(0, kCaveCell));
    REQUIRE_FALSE(registry.IsOccupied(kCaveUpper));

    registry.RemovePlayer(kBob); // never seen
    REQUIRE(registry.SetPlayerCell(kBob, 0));
    REQUIRE(registry.GetOccupantCount(kCaveUpper) == 0);
    REQUIRE_FALSE(registry.IsOccupied(RenewableEncounterId{0x9999u, 0}));
}
