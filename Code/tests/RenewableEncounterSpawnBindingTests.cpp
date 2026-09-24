#include <Services/RenewableEncounterSpawnBinding.h>

#include <catch2/catch.hpp>

namespace
{
constexpr RenewableEncounterId kBarrow{0x000371DEu, 0};
constexpr std::uint32_t kDraugrRef = 0x000380B3u;
constexpr std::uint32_t kSkeeverRef = 0x0003B9E6u;
constexpr std::uint32_t kUnrelatedRef = 0x00012345u;

using Binding = PlacedActorBindResult;

RenewableEncounterRegistry MakeRegistry()
{
    RenewableEncounterRegistry registry;
    REQUIRE(registry.AddEncounter(kBarrow, RenewableEncounterPolicy{60}));
    REQUIRE(registry.AddSlot(kBarrow, SpawnSlotId{kDraugrRef}));
    REQUIRE(registry.AddSlot(kBarrow, SpawnSlotId{kSkeeverRef}));
    return registry;
}
} // namespace

TEST_CASE("W13: a placed actor in a configured slot binds to the current epoch", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(BindPlacedActor(registry, kDraugrRef, {100, 1}, false) == Binding::Bound);
    REQUIRE(registry.GetIncarnationStatus({100, 1}) == RenewableEncounterRegistry::IncarnationStatus::Current);
    REQUIRE(registry.FindEncounter(EncounterIncarnation{100, 1}) == kBarrow);
}

TEST_CASE("W13: actors outside any encounter are ignored", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(BindPlacedActor(registry, kUnrelatedRef, {100, 1}, false) == Binding::NotEncounterSlot);
    REQUIRE(BindPlacedActor(registry, 0, {101, 1}, false) == Binding::NotEncounterSlot);
    REQUIRE(registry.GetIncarnationStatus({100, 1}) == RenewableEncounterRegistry::IncarnationStatus::Unknown);
}

TEST_CASE("W13: an actor that arrives dead is not bound", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(BindPlacedActor(registry, kDraugrRef, {100, 1}, true) == Binding::ArrivedDead);
    REQUIRE(registry.GetIncarnationStatus({100, 1}) == RenewableEncounterRegistry::IncarnationStatus::Unknown);
}

TEST_CASE("W13: a second actor for an occupied slot is rejected", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(BindPlacedActor(registry, kDraugrRef, {100, 1}, false) == Binding::Bound);
    REQUIRE(BindPlacedActor(registry, kDraugrRef, {200, 2}, false) == Binding::Rejected);
    REQUIRE(registry.FindEncounter(EncounterIncarnation{200, 2}) == std::nullopt);
}

TEST_CASE("W13: spawn, kill, clear, reset and respawn through placed actors", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(BindPlacedActor(registry, kDraugrRef, {100, 1}, false) == Binding::Bound);
    REQUIRE(BindPlacedActor(registry, kSkeeverRef, {101, 2}, false) == Binding::Bound);

    REQUIRE(registry.RecordVerifiedDeath({100, 1}, 10) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(registry.RecordVerifiedDeath({101, 2}, 11) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(registry.Find(kBarrow)->IsCleared());

    // The corpses unload; releasing a dead incarnation keeps the slot dead.
    REQUIRE(ReleasePlacedActor(registry, {100, 1}));
    REQUIRE(registry.Find(kBarrow)->IsCleared());

    // A player re-entering before the reset sees corpses, which must not bind.
    REQUIRE(BindPlacedActor(registry, kDraugrRef, {300, 3}, true) == Binding::ArrivedDead);

    REQUIRE(registry.TryReset(kBarrow, 11 + 60));
    REQUIRE(registry.GetIncarnationStatus({101, 2}) == RenewableEncounterRegistry::IncarnationStatus::Stale);

    // After the reset the same placed references bind fresh incarnations in epoch 1.
    REQUIRE(BindPlacedActor(registry, kDraugrRef, {400, 4}, false) == Binding::Bound);
    REQUIRE(BindPlacedActor(registry, kSkeeverRef, {401, 5}, false) == Binding::Bound);
    REQUIRE(registry.Find(kBarrow)->GetEpoch() == 1);
    REQUIRE_FALSE(registry.Find(kBarrow)->IsCleared());
}

TEST_CASE("W13: an actor unloading alive frees its slot for the next load", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    REQUIRE(BindPlacedActor(registry, kDraugrRef, {100, 1}, false) == Binding::Bound);
    REQUIRE(ReleasePlacedActor(registry, {100, 1}));
    REQUIRE(BindPlacedActor(registry, kDraugrRef, {500, 6}, false) == Binding::Bound);
    REQUIRE_FALSE(ReleasePlacedActor(registry, {999, 9}));
}

TEST_CASE("W14: a reset marks every actor bound to the old epoch for removal", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    std::unordered_map<std::uint32_t, EncounterIncarnation> bound;
    REQUIRE(BindPlacedActor(registry, kDraugrRef, {100, 1}, false) == Binding::Bound);
    REQUIRE(BindPlacedActor(registry, kSkeeverRef, {101, 2}, false) == Binding::Bound);
    bound[100] = {100, 1};
    bound[101] = {101, 2};

    REQUIRE(CollectStaleBoundActors(registry, bound).empty());

    REQUIRE(registry.RecordVerifiedDeath({100, 1}, 10) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(registry.RecordVerifiedDeath({101, 2}, 11) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(CollectStaleBoundActors(registry, bound).empty());

    REQUIRE(registry.TryReset(kBarrow, 11 + 60));
    REQUIRE(CollectStaleBoundActors(registry, bound) == std::vector<std::uint32_t>{100, 101});
}

TEST_CASE("W14: actors of the new epoch and unknown actors are never removed", "[renewable_encounter]")
{
    auto registry = MakeRegistry();
    std::unordered_map<std::uint32_t, EncounterIncarnation> bound;
    REQUIRE(BindPlacedActor(registry, kDraugrRef, {100, 1}, false) == Binding::Bound);
    REQUIRE(BindPlacedActor(registry, kSkeeverRef, {101, 2}, false) == Binding::Bound);
    bound[100] = {100, 1};
    REQUIRE(registry.RecordVerifiedDeath({100, 1}, 10) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(registry.RecordVerifiedDeath({101, 2}, 10) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(registry.TryReset(kBarrow, 70));

    REQUIRE(BindPlacedActor(registry, kDraugrRef, {400, 4}, false) == Binding::Bound);
    bound[400] = {400, 4};
    bound[999] = {999, 9};

    REQUIRE(CollectStaleBoundActors(registry, bound) == std::vector<std::uint32_t>{100});
}
