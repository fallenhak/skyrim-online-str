#include <Services/RenewableEncounterState.h>

#include <catch2/catch.hpp>

namespace
{
constexpr RenewableEncounterId kEncounter{0x0001A2B3u, 1};
constexpr SpawnSlotId kSlotA{0x0010F00Du};
constexpr SpawnSlotId kSlotB{0x0010F00Eu};

RenewableEncounterState MakeTwoSlotEncounter(const std::uint64_t aCooldownTicks = 100)
{
    RenewableEncounterState state{kEncounter, RenewableEncounterPolicy{aCooldownTicks}};
    REQUIRE(state.AddSlot(kSlotA));
    REQUIRE(state.AddSlot(kSlotB));
    return state;
}
} // namespace

TEST_CASE("W01: encounter and slot identities reject invalid values", "[renewable_encounter]")
{
    REQUIRE_FALSE(RenewableEncounterId{}.IsValid());
    REQUIRE_FALSE(SpawnSlotId{}.IsValid());
    REQUIRE(kEncounter.IsValid());
    REQUIRE(kSlotA.IsValid());

    RenewableEncounterState state{kEncounter, RenewableEncounterPolicy{}};
    REQUIRE_FALSE(state.AddSlot(SpawnSlotId{}));
    REQUIRE(state.AddSlot(kSlotA));
    REQUIRE_FALSE(state.AddSlot(kSlotA)); // duplicate logical slot
    REQUIRE(state.GetSlotCount() == 1);
}

TEST_CASE("W01: a slot binds exactly one live incarnation", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter();

    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}, state.GetEpoch()));
    REQUIRE_FALSE(state.BindIncarnation(kSlotA, {8, 11}, state.GetEpoch()));              // slot already occupied
    REQUIRE_FALSE(state.BindIncarnation(kSlotB, {7, 10}, state.GetEpoch()));              // incarnation already bound elsewhere
    REQUIRE_FALSE(state.BindIncarnation(kSlotB, {9, 0}, state.GetEpoch()));               // invalid lifecycle generation
    REQUIRE_FALSE(state.BindIncarnation(SpawnSlotId{0x1234}, {9, 12}, state.GetEpoch())); // unknown slot
    REQUIRE(state.FindSlot({7, 10}) == kSlotA);
}

TEST_CASE("W02: verified death only applies to the current incarnation", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter();
    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}, state.GetEpoch()));

    // Same server id, different generation: a stale or foreign incarnation.
    REQUIRE(state.RecordVerifiedDeath({7, 9}, 50) == RenewableEncounterState::DeathResult::UnknownIncarnation);
    REQUIRE(state.GetSlotStatus(kSlotA) == RenewableEncounterState::SlotStatus::Alive);

    REQUIRE(state.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.GetSlotStatus(kSlotA) == RenewableEncounterState::SlotStatus::Dead);

    // Replayed death is idempotent and does not move the death tick.
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 60) == RenewableEncounterState::DeathResult::AlreadyDead);
}

TEST_CASE("W02: encounter is cleared only when every slot is dead", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter();
    REQUIRE_FALSE(state.IsCleared()); // unpopulated is not cleared

    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}, state.GetEpoch()));
    REQUIRE(state.BindIncarnation(kSlotB, {8, 11}, state.GetEpoch()));
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE_FALSE(state.IsCleared());

    REQUIRE(state.RecordVerifiedDeath({8, 11}, 70) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.IsCleared());
    REQUIRE(state.GetClearedTick() == 70);
}

TEST_CASE("W02: an unbound slot keeps the encounter from being cleared", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter();
    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}, state.GetEpoch()));
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE_FALSE(state.IsCleared());
}

TEST_CASE("W04: reset becomes eligible only after the cooldown", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter(100);
    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}, state.GetEpoch()));
    REQUIRE(state.BindIncarnation(kSlotB, {8, 11}, state.GetEpoch()));
    REQUIRE_FALSE(state.IsResetEligible(1000)); // not cleared

    REQUIRE(state.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.RecordVerifiedDeath({8, 11}, 70) == RenewableEncounterState::DeathResult::Recorded);

    REQUIRE_FALSE(state.IsResetEligible(169));
    REQUIRE(state.IsResetEligible(170));
    REQUIRE_FALSE(state.IsResetEligible(10)); // clock earlier than clear tick is never eligible
}

TEST_CASE("W04: reset empties slots and makes old incarnations stale", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter(100);
    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}, state.GetEpoch()));
    REQUIRE(state.BindIncarnation(kSlotB, {8, 11}, state.GetEpoch()));
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.RecordVerifiedDeath({8, 11}, 70) == RenewableEncounterState::DeathResult::Recorded);

    REQUIRE_FALSE(state.TryReset(100)); // cooldown not elapsed
    REQUIRE(state.GetResetCount() == 0);

    REQUIRE(state.TryReset(170));
    REQUIRE(state.GetResetCount() == 1);
    REQUIRE_FALSE(state.IsCleared());
    REQUIRE(state.GetSlotStatus(kSlotA) == RenewableEncounterState::SlotStatus::Unbound);
    REQUIRE_FALSE(state.TryReset(500)); // second reset needs a new clear

    // A late packet for the previous incarnation cannot touch the new one.
    REQUIRE(state.BindIncarnation(kSlotA, {7, 12}, state.GetEpoch())); // EnTT id reused, fresh generation
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 180) == RenewableEncounterState::DeathResult::UnknownIncarnation);
    REQUIRE(state.GetSlotStatus(kSlotA) == RenewableEncounterState::SlotStatus::Alive);
}

TEST_CASE("W04: a zero cooldown still requires a completed clear", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter(0);
    REQUIRE_FALSE(state.TryReset(0));
    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}, state.GetEpoch()));
    REQUIRE(state.BindIncarnation(kSlotB, {8, 11}, state.GetEpoch()));
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 5) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.RecordVerifiedDeath({8, 11}, 5) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.TryReset(5));
}

TEST_CASE("W02: an incarnation released without dying frees its slot", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter();
    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}, state.GetEpoch()));

    // Despawn / cell unload / lost ownership: the creature is gone but did not die.
    REQUIRE(state.ReleaseIncarnation({7, 10}) == RenewableEncounterState::ReleaseResult::Released);
    REQUIRE(state.GetSlotStatus(kSlotA) == RenewableEncounterState::SlotStatus::Unbound);
    REQUIRE_FALSE(state.FindSlot({7, 10}));

    // Its late death can no longer count toward clearing the encounter.
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::UnknownIncarnation);

    // The slot can be refilled by a fresh incarnation.
    REQUIRE(state.BindIncarnation(kSlotA, {9, 20}, state.GetEpoch()));
}

TEST_CASE("W02: releasing a dead or unknown incarnation changes nothing", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter();
    REQUIRE(state.ReleaseIncarnation({7, 10}) == RenewableEncounterState::ReleaseResult::UnknownIncarnation);

    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}, state.GetEpoch()));
    REQUIRE(state.BindIncarnation(kSlotB, {8, 11}, state.GetEpoch()));
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::Recorded);

    // A corpse being unloaded must not undo the verified death.
    REQUIRE(state.ReleaseIncarnation({7, 10}) == RenewableEncounterState::ReleaseResult::AlreadyDead);
    REQUIRE(state.GetSlotStatus(kSlotA) == RenewableEncounterState::SlotStatus::Dead);

    REQUIRE(state.RecordVerifiedDeath({8, 11}, 70) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.IsCleared());
}

TEST_CASE("W02: a spawn requested before a reset cannot bind after it", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter(0);
    const auto epochBeforeReset = state.GetEpoch();

    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}, epochBeforeReset));
    REQUIRE(state.BindIncarnation(kSlotB, {8, 11}, epochBeforeReset));
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 5) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.RecordVerifiedDeath({8, 11}, 5) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.TryReset(5));
    REQUIRE(state.GetEpoch() == epochBeforeReset + 1);

    REQUIRE_FALSE(state.BindIncarnation(kSlotA, {9, 12}, epochBeforeReset));
    REQUIRE(state.GetSlotStatus(kSlotA) == RenewableEncounterState::SlotStatus::Unbound);
    REQUIRE(state.BindIncarnation(kSlotA, {9, 12}, state.GetEpoch()));
}
