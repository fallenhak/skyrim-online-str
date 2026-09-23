#include <Services/RenewableEncounterRegistry.h>

#include <catch2/catch.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <random>

namespace
{
constexpr std::array<RenewableEncounterId, 2> kEncounters{RenewableEncounterId{0x0001A2B3u, 0}, RenewableEncounterId{0x0001A2B3u, 1}};
constexpr std::size_t kSlotsPerEncounter = 3;

constexpr SpawnSlotId SlotOf(const std::size_t aEncounter, const std::size_t aSlot)
{
    return SpawnSlotId{static_cast<std::uint32_t>(0x00200000u + aEncounter * 0x10u + aSlot)};
}

// What the test expects to know about every incarnation it ever bound.
struct Expected final
{
    RenewableEncounterId Encounter{};
    std::uint64_t Epoch{};
    bool Released{};
    bool Dead{};
};

void CheckInvariants(const RenewableEncounterRegistry& acRegistry, const std::map<EncounterIncarnation, Expected>& acExpected)
{
    for (const auto& id : kEncounters)
    {
        const auto* pEncounter = acRegistry.Find(id);
        REQUIRE(pEncounter != nullptr);

        const auto membership = pEncounter->GetMembership();
        REQUIRE(membership.Unbound + membership.Alive + membership.Dead == kSlotsPerEncounter);
        REQUIRE(pEncounter->IsCleared() == (membership.Dead == kSlotsPerEncounter));
    }

    for (const auto& [incarnation, expected] : acExpected)
    {
        const auto* pEncounter = acRegistry.Find(expected.Encounter);
        const bool current = expected.Epoch == pEncounter->GetEpoch() && !expected.Released;

        // Stale or released incarnations are unknown everywhere; current ones are known exactly once.
        REQUIRE(acRegistry.FindEncounter(incarnation).has_value() == current);
        REQUIRE(pEncounter->FindSlot(incarnation).has_value() == current);
        if (!current)
            continue;

        REQUIRE(acRegistry.FindEncounter(incarnation) == expected.Encounter);
        const auto status = pEncounter->GetSlotStatus(*pEncounter->FindSlot(incarnation));
        REQUIRE(status == (expected.Dead ? RenewableEncounterState::SlotStatus::Dead : RenewableEncounterState::SlotStatus::Alive));
    }
}

void RunRandomSequence(const std::uint32_t aSeed, const std::uint64_t aCooldownTicks)
{
    RenewableEncounterRegistry registry;
    for (std::size_t e = 0; e < kEncounters.size(); ++e)
    {
        REQUIRE(registry.AddEncounter(kEncounters[e], RenewableEncounterPolicy{aCooldownTicks}));
        for (std::size_t s = 0; s < kSlotsPerEncounter; ++s)
            REQUIRE(registry.AddSlot(kEncounters[e], SlotOf(e, s)));
    }

    std::mt19937 rng{aSeed};
    const auto pick = [&rng](const std::size_t aCount) { return std::uniform_int_distribution<std::size_t>{0, aCount - 1}(rng); };

    std::map<EncounterIncarnation, Expected> expected;
    std::uint64_t tick = 1;
    std::uint64_t nextGeneration = 1;

    const auto randomKnown = [&]() -> const EncounterIncarnation* {
        if (expected.empty())
            return nullptr;
        auto it = expected.begin();
        std::advance(it, static_cast<std::ptrdiff_t>(pick(expected.size())));
        return &it->first;
    };

    for (int step = 0; step < 500; ++step)
    {
        tick += pick(4);
        const std::size_t e = pick(kEncounters.size());
        const auto& id = kEncounters[e];
        const std::uint64_t epoch = registry.Find(id)->GetEpoch();

        switch (pick(5))
        {
        case 0:
        case 1: // bind a fresh incarnation, sometimes with a stale spawn epoch
        {
            const bool staleSpawn = epoch > 0 && pick(5) == 0;
            const EncounterIncarnation incarnation{static_cast<std::uint32_t>(pick(8)), nextGeneration++};
            const auto slot = SlotOf(e, pick(kSlotsPerEncounter));
            const bool slotFree = registry.Find(id)->GetSlotStatus(slot) == RenewableEncounterState::SlotStatus::Unbound;

            const bool bound = registry.BindIncarnation(id, slot, incarnation, staleSpawn ? epoch - 1 : epoch);
            REQUIRE(bound == (slotFree && !staleSpawn));
            if (bound)
                expected[incarnation] = Expected{id, epoch, false, false};
            break;
        }
        case 2: // verified death of any known incarnation, including stale ones
        {
            if (const auto* pIncarnation = randomKnown())
            {
                auto& known = expected[*pIncarnation];
                const bool current = known.Epoch == registry.Find(known.Encounter)->GetEpoch() && !known.Released;
                const auto result = registry.RecordVerifiedDeath(*pIncarnation, tick);
                if (!current)
                    REQUIRE(result == RenewableEncounterState::DeathResult::UnknownIncarnation);
                else if (known.Dead)
                    REQUIRE(result == RenewableEncounterState::DeathResult::AlreadyDead);
                else
                {
                    REQUIRE(result == RenewableEncounterState::DeathResult::Recorded);
                    known.Dead = true;
                }
            }
            break;
        }
        case 3: // release (despawn / unload / lost ownership)
        {
            if (const auto* pIncarnation = randomKnown())
            {
                auto& known = expected[*pIncarnation];
                const bool current = known.Epoch == registry.Find(known.Encounter)->GetEpoch() && !known.Released;
                const auto result = registry.ReleaseIncarnation(*pIncarnation);
                if (!current)
                    REQUIRE(result == RenewableEncounterState::ReleaseResult::UnknownIncarnation);
                else if (known.Dead)
                    REQUIRE(result == RenewableEncounterState::ReleaseResult::AlreadyDead);
                else
                {
                    REQUIRE(result == RenewableEncounterState::ReleaseResult::Released);
                    known.Released = true;
                }
            }
            break;
        }
        default: // reset attempt
        {
            const bool eligible = registry.Find(id)->IsResetEligible(tick);
            REQUIRE(registry.TryReset(id, tick) == eligible);
            REQUIRE(registry.Find(id)->GetEpoch() == (eligible ? epoch + 1 : epoch));
            if (eligible)
                REQUIRE(registry.Find(id)->GetMembership().Unbound == kSlotsPerEncounter);
            break;
        }
        }

        CheckInvariants(registry, expected);
    }
}
} // namespace

TEST_CASE("W01/W02 stress: random bind/death/release/reset sequences keep invariants", "[renewable_encounter][stress]")
{
    for (const std::uint32_t seed : {1u, 7u, 42u, 1337u, 90210u})
    {
        for (const std::uint64_t cooldown : {0ull, 5ull})
        {
            INFO("seed=" << seed << " cooldown=" << cooldown);
            RunRandomSequence(seed, cooldown);
        }
    }
}
