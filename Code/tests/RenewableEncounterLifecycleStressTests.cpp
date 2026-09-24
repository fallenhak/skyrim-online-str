#include <Services/RenewableEncounterRegistry.h>

#include <catch2/catch.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <vector>

namespace
{
constexpr std::array<RenewableEncounterId, 3> kEncounters{RenewableEncounterId{0x0001A2B3u, 0}, RenewableEncounterId{0x0001A2B3u, 1}, RenewableEncounterId{0x0001A2B4u, 0}};
constexpr std::size_t kSlotsPerEncounter = 2;
constexpr std::array<std::uint32_t, 3> kPlayers{1, 2, 3};
constexpr std::uint64_t kTicketTtl = 20;

using Registry = RenewableEncounterRegistry;
using Death = RenewableEncounterState::DeathResult;

constexpr SpawnSlotId SlotOf(const std::size_t aEncounter, const std::size_t aSlot)
{
    return SpawnSlotId{static_cast<std::uint32_t>(0x00300000u + aEncounter * 0x10u + aSlot)};
}

Registry MakeRegistry(const std::uint64_t aCooldown)
{
    Registry registry;
    for (std::size_t e = 0; e < kEncounters.size(); ++e)
    {
        REQUIRE(registry.AddEncounter(kEncounters[e], RenewableEncounterPolicy{aCooldown}));
        for (std::size_t s = 0; s < kSlotsPerEncounter; ++s)
            REQUIRE(registry.AddSlot(kEncounters[e], SlotOf(e, s)));
    }
    return registry;
}

// Invariants that must hold after every step, whatever happened before.
void CheckInvariants(const Registry& acRegistry, const std::map<SpawnSlotId, Registry::SpawnTicket>& acTickets)
{
    for (std::size_t e = 0; e < kEncounters.size(); ++e)
    {
        const auto* pEncounter = acRegistry.Find(kEncounters[e]);
        REQUIRE(pEncounter != nullptr);
        const auto membership = pEncounter->GetMembership();
        REQUIRE(membership.Unbound + membership.Alive + membership.Dead == kSlotsPerEncounter);
        REQUIRE(pEncounter->IsCleared() == (membership.Dead == kSlotsPerEncounter));

        // No slot is ever both requested and claimed, and requests carry the current epoch.
        const auto requests = acRegistry.GetSpawnRequests(kEncounters[e]);
        REQUIRE(requests.size() + [&] {
            std::size_t claimed = 0;
            for (std::size_t s = 0; s < kSlotsPerEncounter; ++s)
                claimed += acTickets.count(SlotOf(e, s));
            return claimed;
        }() == (pEncounter->IsCleared() ? 0 : membership.Unbound));
        for (const auto& request : requests)
        {
            REQUIRE(request.Epoch == pEncounter->GetEpoch());
            REQUIRE(acTickets.count(request.Slot) == 0);
        }
    }
}

struct Run final
{
    explicit Run(const std::uint32_t aSeed, const std::uint64_t aCooldown)
        : Cooldown(aCooldown)
        , Rng(aSeed)
        , Current(MakeRegistry(aCooldown))
    {
    }

    std::size_t Pick(const std::size_t aCount) { return std::uniform_int_distribution<std::size_t>{0, aCount - 1}(Rng); }

    void Step()
    {
        Tick += Pick(5);
        const std::size_t e = Pick(kEncounters.size());
        const auto& id = kEncounters[e];
        const auto slot = SlotOf(e, Pick(kSlotsPerEncounter));
        const auto epoch = Current.Find(id)->GetEpoch();

        switch (Pick(8))
        {
        case 0: // claim; a second claim on the same slot must fail
        {
            const auto player = kPlayers[Pick(kPlayers.size())];
            const bool claimable = Tickets.count(slot) == 0 && !Current.Find(id)->IsCleared() &&
                                   Current.Find(id)->GetSlotStatus(slot) == RenewableEncounterState::SlotStatus::Unbound;
            const auto ticket = Current.ClaimSpawn(id, slot, epoch, player, Tick);
            REQUIRE(ticket.has_value() == claimable);
            if (ticket)
            {
                Tickets.emplace(slot, *ticket);
                REQUIRE_FALSE(Current.ClaimSpawn(id, slot, epoch, kPlayers[Pick(kPlayers.size())], Tick));
            }
            break;
        }
        case 1: // complete a live or dead ticket
        {
            if (Tickets.empty() && Voided.empty())
                break;
            const bool useVoided = !Voided.empty() && (Tickets.empty() || Pick(3) == 0);
            const auto ticket = useVoided ? Voided[Pick(Voided.size())] : std::next(Tickets.begin(), static_cast<std::ptrdiff_t>(Pick(Tickets.size())))->second;
            const EncounterIncarnation fresh{static_cast<std::uint32_t>(Pick(16)), NextGeneration++};
            const bool completed = Current.CompleteSpawn(ticket, fresh);
            REQUIRE(completed == !useVoided);
            if (completed)
            {
                Tickets.erase(ticket.Slot);
                Alive.push_back(fresh);
                // Completing twice never double-spawns.
                REQUIRE_FALSE(Current.CompleteSpawn(ticket, {fresh.ServerId, NextGeneration++}));
            }
            break;
        }
        case 2: // verified death of a live incarnation, or a stale one
        {
            if (Alive.empty())
                break;
            const auto incarnation = Alive[Pick(Alive.size())];
            const auto status = Current.GetIncarnationStatus(incarnation);
            const auto result = Current.RecordVerifiedDeath(incarnation, Tick);
            if (status == Registry::IncarnationStatus::Stale)
                REQUIRE(result == Death::StaleIncarnation);
            else
                REQUIRE((result == Death::Recorded || result == Death::AlreadyDead));
            break;
        }
        case 3: // disconnect: claims void, late completions refused
        {
            const auto player = kPlayers[Pick(kPlayers.size())];
            std::size_t expected = 0;
            for (auto it = Tickets.begin(); it != Tickets.end();)
            {
                if (it->second.Owner == player)
                {
                    Voided.push_back(it->second);
                    it = Tickets.erase(it);
                    ++expected;
                }
                else
                    ++it;
            }
            REQUIRE(Current.ReleasePlayerClaims(player) == expected);
            break;
        }
        case 4: // claim timeout
        {
            std::size_t expected = 0;
            for (auto it = Tickets.begin(); it != Tickets.end();)
            {
                if (Tick - ClaimTick(it->second) >= kTicketTtl)
                {
                    Voided.push_back(it->second);
                    it = Tickets.erase(it);
                    ++expected;
                }
                else
                    ++it;
            }
            REQUIRE(Current.ExpireSpawnClaims(Tick, kTicketTtl) == expected);
            break;
        }
        case 5: // reset attempt
        {
            const bool eligible = Current.GetResetBlocker(id, Tick) == Registry::ResetBlocker::None;
            REQUIRE(Current.TryReset(id, Tick) == eligible);
            if (eligible)
            {
                REQUIRE(Current.Find(id)->GetEpoch() == epoch + 1);
                for (std::size_t s = 0; s < kSlotsPerEncounter; ++s)
                    REQUIRE(Tickets.count(SlotOf(e, s)) == 0); // a cleared encounter never had open claims
            }
            break;
        }
        case 6: // server restart: snapshot, reload from config, restore with ticks starting over
        {
            const auto snapshot = Current.Snapshot(Tick);
            std::map<RenewableEncounterId, std::pair<std::uint64_t, std::uint64_t>> before;
            for (const auto& entry : snapshot)
                before[entry.Id] = {entry.Epoch, entry.CooldownRemainingTicks};

            Registry restarted = MakeRegistry(Cooldown);
            const std::uint64_t newTick = Pick(3);
            REQUIRE(restarted.Restore(snapshot, newTick));
            for (const auto& [encounterId, values] : before)
            {
                REQUIRE(restarted.Find(encounterId)->GetEpoch() == values.first + 1);
                REQUIRE(restarted.Find(encounterId)->GetResetCooldownRemaining(newTick) == values.second);
            }

            // Everything issued before the restart is dead.
            for (const auto& [ticketSlot, ticket] : Tickets)
                Voided.push_back(ticket);
            Tickets.clear();
            for (const auto& incarnation : Alive)
                REQUIRE(restarted.GetIncarnationStatus(incarnation) == Registry::IncarnationStatus::Unknown);
            Alive.clear();

            Current = std::move(restarted);
            TickBase.clear();
            Tick = newTick;
            break;
        }
        default: // players move around, which gates resets
        {
            const auto player = kPlayers[Pick(kPlayers.size())];
            const std::uint32_t cell = Pick(3) == 0 ? 0 : kEncounters[Pick(kEncounters.size())].CellFormId;
            REQUIRE(Current.SetPlayerCell(player, cell));
            break;
        }
        }

        for (const auto& [ticketSlot, ticket] : Tickets)
            TickBase.emplace(ticket.Id, Tick);
        CheckInvariants(Current, Tickets);
    }

    std::uint64_t ClaimTick(const Registry::SpawnTicket& acTicket) const { return TickBase.at(acTicket.Id); }

    std::uint64_t Cooldown;
    std::mt19937 Rng;
    Registry Current;
    std::uint64_t Tick{1};
    std::uint64_t NextGeneration{1};
    std::map<SpawnSlotId, Registry::SpawnTicket> Tickets;
    std::map<std::uint64_t, std::uint64_t> TickBase;
    std::vector<Registry::SpawnTicket> Voided;
    std::vector<EncounterIncarnation> Alive;
};
} // namespace

TEST_CASE("W09 stress: claims, spawns, deaths, resets and restarts keep every invariant", "[renewable_encounter][stress]")
{
    for (const std::uint64_t cooldown : {std::uint64_t{0}, std::uint64_t{7}})
    {
        for (std::uint32_t seed = 1; seed <= 25; ++seed)
        {
            INFO("seed=" << seed << " cooldown=" << cooldown);
            Run run{seed, cooldown};
            for (int step = 0; step < 400; ++step)
                run.Step();
        }
    }
}

TEST_CASE("W09: the same seed replays the same history", "[renewable_encounter][stress]")
{
    Run first{42, 7};
    Run second{42, 7};
    for (int step = 0; step < 400; ++step)
    {
        first.Step();
        second.Step();
    }

    const auto a = first.Current.Snapshot(first.Tick);
    const auto b = second.Current.Snapshot(second.Tick);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE(a[i].Id == b[i].Id);
        REQUIRE(a[i].Epoch == b[i].Epoch);
        REQUIRE(a[i].Cleared == b[i].Cleared);
        REQUIRE(a[i].CooldownRemainingTicks == b[i].CooldownRemainingTicks);
    }
}

TEST_CASE("W09: a snapshot naming an encounter twice is rejected without a partial restore", "[renewable_encounter]")
{
    auto registry = MakeRegistry(0);
    const std::vector<Registry::EncounterSnapshot> duplicate{
        {kEncounters[0], 3, false, 0},
        {kEncounters[1], 4, false, 0},
        {kEncounters[0], 5, false, 0},
    };
    REQUIRE_FALSE(registry.Restore(duplicate, 0));
    for (const auto& id : kEncounters)
        REQUIRE(registry.Find(id)->GetEpoch() == 0);
}

TEST_CASE("W09: malformed snapshot entries are rejected as a whole", "[renewable_encounter]")
{
    const auto rejects = [](const std::vector<Registry::EncounterSnapshot>& acSnapshot) {
        auto registry = MakeRegistry(0);
        REQUIRE_FALSE(registry.Restore(acSnapshot, 0));
        for (const auto& id : kEncounters)
        {
            REQUIRE(registry.Find(id)->GetEpoch() == 0);
            REQUIRE_FALSE(registry.Find(id)->IsCleared());
        }
    };

    rejects({{kEncounters[0], 1, false, 0}, {RenewableEncounterId{0x0BADu, 0}, 0, false, 0}}); // unknown encounter
    rejects({{kEncounters[0], 1, false, 0}, {RenewableEncounterId{0, 0}, 0, false, 0}});       // invalid id
    rejects({{kEncounters[0], 1, false, 9}});                                                   // live encounter with a cooldown
    rejects({{kEncounters[0], std::numeric_limits<std::uint64_t>::max(), false, 0}});          // epoch cannot advance
}

TEST_CASE("W09: a snapshot may omit encounters, which then start fresh", "[renewable_encounter]")
{
    auto registry = MakeRegistry(0);
    REQUIRE(registry.Restore({{kEncounters[2], 4, true, 0}}, 0));
    REQUIRE(registry.Find(kEncounters[2])->GetEpoch() == 5);
    REQUIRE(registry.Find(kEncounters[2])->IsCleared());
    REQUIRE(registry.Find(kEncounters[0])->GetEpoch() == 0);
    REQUIRE(registry.GetSpawnRequests(kEncounters[0]).size() == kSlotsPerEncounter);
}
