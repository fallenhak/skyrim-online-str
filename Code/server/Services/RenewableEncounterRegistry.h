#pragma once

#include <Services/RenewableEncounterState.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

/**
 * Owns every renewable encounter on the server and keeps two invariants
 * across them (roadmap W01, W02):
 *  - a placed reference (spawn slot) belongs to exactly one encounter;
 *  - a live or dead incarnation belongs to at most one encounter.
 *
 * All mutations go through the registry so its incarnation index cannot drift
 * from the encounters; callers only get const access to an encounter.
 */
class RenewableEncounterRegistry final
{
public:
    [[nodiscard]] bool AddEncounter(const RenewableEncounterId aId, const RenewableEncounterPolicy aPolicy)
    {
        if (!aId.IsValid())
            return false;

        return m_encounters.emplace(aId, RenewableEncounterState{aId, aPolicy}).second;
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
        if (!pEncounter || FindEncounter(aIncarnation))
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
            return RenewableEncounterState::DeathResult::UnknownIncarnation;

        return pEncounter->RecordVerifiedDeath(aIncarnation, aTick);
    }

    [[nodiscard]] RenewableEncounterState::ReleaseResult ReleaseIncarnation(const EncounterIncarnation aIncarnation)
    {
        auto* pEncounter = FindOwner(aIncarnation);
        if (!pEncounter)
            return RenewableEncounterState::ReleaseResult::UnknownIncarnation;

        const auto result = pEncounter->ReleaseIncarnation(aIncarnation);
        if (result == RenewableEncounterState::ReleaseResult::Released)
            m_encounterByIncarnation.erase(aIncarnation);

        return result;
    }

    [[nodiscard]] bool TryReset(const RenewableEncounterId aId, const std::uint64_t aNowTick)
    {
        auto* pEncounter = FindMutable(aId);
        if (!pEncounter || !pEncounter->TryReset(aNowTick))
            return false;

        for (auto it = m_encounterByIncarnation.begin(); it != m_encounterByIncarnation.end();)
        {
            if (it->second == aId)
                it = m_encounterByIncarnation.erase(it);
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
};
