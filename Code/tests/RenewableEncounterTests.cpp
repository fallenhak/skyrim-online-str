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

    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}));
    REQUIRE_FALSE(state.BindIncarnation(kSlotA, {8, 11}));              // slot already occupied
    REQUIRE_FALSE(state.BindIncarnation(kSlotB, {7, 10}));              // incarnation already bound elsewhere
    REQUIRE_FALSE(state.BindIncarnation(kSlotB, {9, 0}));               // invalid lifecycle generation
    REQUIRE_FALSE(state.BindIncarnation(SpawnSlotId{0x1234}, {9, 12})); // unknown slot
    REQUIRE(state.FindSlot({7, 10}) == kSlotA);
}

TEST_CASE("W02: verified death only applies to the current incarnation", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter();
    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}));

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

    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}));
    REQUIRE(state.BindIncarnation(kSlotB, {8, 11}));
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE_FALSE(state.IsCleared());

    REQUIRE(state.RecordVerifiedDeath({8, 11}, 70) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.IsCleared());
    REQUIRE(state.GetClearedTick() == 70);
}

TEST_CASE("W02: an unbound slot keeps the encounter from being cleared", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter();
    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}));
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 50) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE_FALSE(state.IsCleared());
}

TEST_CASE("W04: reset becomes eligible only after the cooldown", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter(100);
    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}));
    REQUIRE(state.BindIncarnation(kSlotB, {8, 11}));
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
    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}));
    REQUIRE(state.BindIncarnation(kSlotB, {8, 11}));
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
    REQUIRE(state.BindIncarnation(kSlotA, {7, 12})); // EnTT id reused, fresh generation
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 180) == RenewableEncounterState::DeathResult::UnknownIncarnation);
    REQUIRE(state.GetSlotStatus(kSlotA) == RenewableEncounterState::SlotStatus::Alive);
}

TEST_CASE("W04: a zero cooldown still requires a completed clear", "[renewable_encounter]")
{
    auto state = MakeTwoSlotEncounter(0);
    REQUIRE_FALSE(state.TryReset(0));
    REQUIRE(state.BindIncarnation(kSlotA, {7, 10}));
    REQUIRE(state.BindIncarnation(kSlotB, {8, 11}));
    REQUIRE(state.RecordVerifiedDeath({7, 10}, 5) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.RecordVerifiedDeath({8, 11}, 5) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(state.TryReset(5));
}
