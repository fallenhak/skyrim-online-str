#pragma once

#include <Services/RenewableEncounterState.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

/**
 * Owns every renewable encounter on the server and keeps two invariants
 * across them (roadmap W01, W02):
 *  - a placed reference (spawn slot) belongs to exactly one encounter;
 *  - a live or dead incarnation belongs to at most one encounter.
 *
 * All mutations go through the registry so its incarnation index cannot drift
 * from the encounters; callers only get const access to an encounter.
 *
 * It also tracks which cell each connected player is in (roadmap W05). An
 * encounter covers a server-defined set of cells: its owning cell plus any
 * cell added with AddEncounterCell (a dungeon spanning several interiors). It
 * is occupied while any player is in one of those cells, which covers every
 * group sharing them, and an occupied encounter cannot reset. The caller must
 * feed SetPlayerCell from the server's own cell tracking, never from a client
 * claim, and call RemovePlayer on disconnect.
 *
 * Every incarnation that leaves an encounter, through a reset or a release,
 * is retired (roadmap W06). Packets that still carry a retired incarnation are
 * reported as stale and never touch the encounter again, and a retired
 * incarnation can never be bound again: a respawn must be a fresh incarnation
 * requested through GetSpawnRequests for the current epoch. The retired set
 * grows with every spawned actor; it is in-memory and cleared by a restart,
 * where the combat lane issues new lifecycle generations anyway.
 */
class RenewableEncounterRegistry final
{
public:
    enum class ResetBlocker : std::uint8_t
    {
        None,
        UnknownEncounter,
        NotEligible,
        Occupied
    };

    enum class IncarnationStatus : std::uint8_t
    {
        Unknown,
        Current,
        Stale
    };

    struct RetiredIncarnation final
    {
        RenewableEncounterId Encounter{};
        std::uint64_t Epoch{};
    };

    struct SpawnRequest final
    {
        SpawnSlotId Slot{};
        std::uint64_t Epoch{};
    };

    [[nodiscard]] bool AddEncounter(const RenewableEncounterId aId, const RenewableEncounterPolicy aPolicy)
    {
        if (!aId.IsValid())
            return false;

        if (!m_encounters.emplace(aId, RenewableEncounterState{aId, aPolicy}).second)
            return false;

        m_cellsByEncounter[aId].insert(aId.CellFormId);
        return true;
    }

    /**
     * Extends an encounter's occupancy scope with another cell. Cells come from
     * server configuration only, never from a client-provided list.
     */
    [[nodiscard]] bool AddEncounterCell(const RenewableEncounterId aId, const std::uint32_t aCellFormId)
    {
        if (aCellFormId == 0 || !Find(aId))
            return false;

        return m_cellsByEncounter[aId].insert(aCellFormId).second;
    }

    [[nodiscard]] bool AddSlot(const RenewableEncounterId aId, const SpawnSlotId aSlot)
    {
        auto* pEncounter = FindMutable(aId);
        if (!pEncounter || m_encounterBySlot.count(aSlot) != 0)
            return false;

        if (!pEncounter->AddSlot(aSlot))
            return false;

        m_encounterBySlot.emplace(aSlot, aId);
        return true;
    }

    [[nodiscard]] const RenewableEncounterState* Find(const RenewableEncounterId aId) const noexcept
    {
        const auto it = m_encounters.find(aId);
        return it == m_encounters.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t GetEncounterCount() const noexcept { return m_encounters.size(); }

    [[nodiscard]] std::optional<RenewableEncounterId> FindEncounter(const EncounterIncarnation aIncarnation) const noexcept
    {
        const auto it = m_encounterByIncarnation.find(aIncarnation);
        if (it == m_encounterByIncarnation.end())
            return std::nullopt;

        return it->second;
    }

    [[nodiscard]] std::optional<RenewableEncounterId> FindEncounter(const SpawnSlotId aSlot) const noexcept
    {
        const auto it = m_encounterBySlot.find(aSlot);
        if (it == m_encounterBySlot.end())
            return std::nullopt;

        return it->second;
    }

    [[nodiscard]] bool BindIncarnation(const RenewableEncounterId aId, const SpawnSlotId aSlot, const EncounterIncarnation aIncarnation, const std::uint64_t aSpawnEpoch)
    {
        auto* pEncounter = FindMutable(aId);
        if (!pEncounter || FindEncounter(aIncarnation) || FindRetired(aIncarnation))
            return false;

        if (!pEncounter->BindIncarnation(aSlot, aIncarnation, aSpawnEpoch))
            return false;

        m_encounterByIncarnation.emplace(aIncarnation, aId);
        return true;
    }

    [[nodiscard]] RenewableEncounterState::DeathResult RecordVerifiedDeath(const EncounterIncarnation aIncarnation, const std::uint64_t aTick)
    {
        auto* pEncounter = FindOwner(aIncarnation);
        if (!pEncounter)
            return FindRetired(aIncarnation) ? RenewableEncounterState::DeathResult::StaleIncarnation : RenewableEncounterState::DeathResult::UnknownIncarnation;

        return pEncounter->RecordVerifiedDeath(aIncarnation, aTick);
    }

    [[nodiscard]] RenewableEncounterState::ReleaseResult ReleaseIncarnation(const EncounterIncarnation aIncarnation)
    {
        auto* pEncounter = FindOwner(aIncarnation);
        if (!pEncounter)
            return FindRetired(aIncarnation) ? RenewableEncounterState::ReleaseResult::StaleIncarnation : RenewableEncounterState::ReleaseResult::UnknownIncarnation;

        const auto result = pEncounter->ReleaseIncarnation(aIncarnation);
        if (result == RenewableEncounterState::ReleaseResult::Released)
        {
            m_retired.emplace(aIncarnation, RetiredIncarnation{pEncounter->GetId(), pEncounter->GetEpoch()});
            m_encounterByIncarnation.erase(aIncarnation);
        }

        return result;
    }

    [[nodiscard]] std::optional<RetiredIncarnation> FindRetired(const EncounterIncarnation aIncarnation) const noexcept
    {
        const auto it = m_retired.find(aIncarnation);
        if (it == m_retired.end())
            return std::nullopt;

        return it->second;
    }

    /**
     * Classifies the incarnation a packet refers to: Current while bound to an
     * encounter, Stale once retired, Unknown if the registry never bound it.
     */
    [[nodiscard]] IncarnationStatus GetIncarnationStatus(const EncounterIncarnation aIncarnation) const noexcept
    {
        if (FindEncounter(aIncarnation))
            return IncarnationStatus::Current;

        return FindRetired(aIncarnation) ? IncarnationStatus::Stale : IncarnationStatus::Unknown;
    }

    /**
     * Slots the spawner should fill, each tagged with the epoch to pass back to
     * BindIncarnation. A request that completes after a reset carries the old
     * epoch and is rejected.
     */
    [[nodiscard]] std::vector<SpawnRequest> GetSpawnRequests(const RenewableEncounterId aId) const
    {
        std::vector<SpawnRequest> requests;
        const auto* pEncounter = Find(aId);
        if (!pEncounter)
            return requests;

        for (const auto slot : pEncounter->GetUnboundSlots())
            requests.push_back(SpawnRequest{slot, pEncounter->GetEpoch()});

        return requests;
    }

    /**
     * Moves a player to a cell; a cell form id of 0 means the player is in no
     * tracked cell. Returns false for an invalid player id.
     */
    [[nodiscard]] bool SetPlayerCell(const std::uint32_t aPlayerId, const std::uint32_t aCellFormId)
    {
        if (aPlayerId == 0)
            return false;

        if (aCellFormId == 0)
            m_cellByPlayer.erase(aPlayerId);
        else
            m_cellByPlayer[aPlayerId] = aCellFormId;

        return true;
    }

    void RemovePlayer(const std::uint32_t aPlayerId) { m_cellByPlayer.erase(aPlayerId); }

    [[nodiscard]] std::size_t GetOccupantCount(const RenewableEncounterId aId) const noexcept
    {
        const auto cells = m_cellsByEncounter.find(aId);
        if (cells == m_cellsByEncounter.end())
            return 0;

        std::size_t count = 0;
        for (const auto& [playerId, cellFormId] : m_cellByPlayer)
        {
            if (cells->second.count(cellFormId) != 0)
                ++count;
        }

        return count;
    }

    [[nodiscard]] bool IsOccupied(const RenewableEncounterId aId) const noexcept { return GetOccupantCount(aId) != 0; }

    [[nodiscard]] ResetBlocker GetResetBlocker(const RenewableEncounterId aId, const std::uint64_t aNowTick) const noexcept
    {
        const auto* pEncounter = Find(aId);
        if (!pEncounter)
            return ResetBlocker::UnknownEncounter;

        if (!pEncounter->IsResetEligible(aNowTick))
            return ResetBlocker::NotEligible;

        if (IsOccupied(aId))
            return ResetBlocker::Occupied;

        return ResetBlocker::None;
    }

    [[nodiscard]] bool TryReset(const RenewableEncounterId aId, const std::uint64_t aNowTick)
    {
        if (GetResetBlocker(aId, aNowTick) != ResetBlocker::None)
            return false;

        auto* pEncounter = FindMutable(aId);
        if (!pEncounter)
            return false;

        const auto retiredEpoch = pEncounter->GetEpoch();
        if (!pEncounter->TryReset(aNowTick))
            return false;

        for (auto it = m_encounterByIncarnation.begin(); it != m_encounterByIncarnation.end();)
        {
            if (it->second == aId)
            {
                m_retired.emplace(it->first, RetiredIncarnation{aId, retiredEpoch});
                it = m_encounterByIncarnation.erase(it);
            }
            else
                ++it;
        }

        return true;
    }

private:
    [[nodiscard]] RenewableEncounterState* FindMutable(const RenewableEncounterId aId) noexcept
    {
        const auto it = m_encounters.find(aId);
        return it == m_encounters.end() ? nullptr : &it->second;
    }

    [[nodiscard]] RenewableEncounterState* FindOwner(const EncounterIncarnation aIncarnation) noexcept
    {
        const auto owner = FindEncounter(aIncarnation);
        return owner ? FindMutable(*owner) : nullptr;
    }

    std::map<RenewableEncounterId, RenewableEncounterState> m_encounters;
    std::map<SpawnSlotId, RenewableEncounterId> m_encounterBySlot;
    std::map<EncounterIncarnation, RenewableEncounterId> m_encounterByIncarnation;
    std::map<RenewableEncounterId, std::set<std::uint32_t>> m_cellsByEncounter;
    std::map<std::uint32_t, std::uint32_t> m_cellByPlayer;
    std::map<EncounterIncarnation, RetiredIncarnation> m_retired;
};
