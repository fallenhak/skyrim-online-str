#pragma once

#include "Records/ACHR.h"
#include "Records/CLMT.h"
#include "Records/CELL.h"
#include "Records/CONT.h"
#include "Records/ECZN.h"
#include "Records/LCTN.h"
#include "Records/GLOB.h"
#include "Records/LVLI.h"
#include "Records/GMST.h"
#include "Records/LVLN.h"
#include "Records/NAVM.h"
#include "Records/NPC.h"
#include "Records/RACE.h"
#include "Records/REFR.h"
#include "Records/WRLD.h"

namespace ESLoader
{
struct RecordCollection
{
    friend class TESFile;

    FormEnum GetFormType(uint32_t aFormId) const noexcept
    {
        auto record = m_allRecords.find(aFormId);
        if (record == std::end(m_allRecords))
        {
            spdlog::error("Record not found for form id {:X}", aFormId);
            return FormEnum::EMPTY_ID;
        }

        return record->second.GetType();
    }

    bool HasAnyRecords() const noexcept { return m_allRecords.size(); }

    REFR& GetObjectRefById(uint32_t aFormId) noexcept { return m_objectReferences[aFormId]; }
    [[nodiscard]] const ACHR* FindActorReferenceById(uint32_t aFormId) const noexcept
    {
        const auto it = m_actorReferences.find(aFormId);
        return it == m_actorReferences.end() ? nullptr : &it->second;
    }

    [[nodiscard]] ACHR* FindActorReferenceById(uint32_t aFormId) noexcept
    {
        auto it = m_actorReferences.find(aFormId);
        return it == m_actorReferences.end() ? nullptr : &it.value();
    }

    CLMT& GetClimateById(uint32_t aFormId) noexcept { return m_climates[aFormId]; }
    NPC& GetNpcById(uint32_t aFormId) noexcept { return m_npcs[aFormId]; }
    [[nodiscard]] const NPC* FindNpcById(uint32_t aFormId) const noexcept
    {
        const auto it = m_npcs.find(aFormId);
        return it == m_npcs.end() ? nullptr : &it->second;
    }

    [[nodiscard]] NPC* FindNpcById(uint32_t aFormId) noexcept
    {
        auto it = m_npcs.find(aFormId);
        return it == m_npcs.end() ? nullptr : &it.value();
    }

    [[nodiscard]] const RACE* FindRaceById(uint32_t aFormId) const noexcept
    {
        const auto it = m_races.find(aFormId);
        return it == m_races.end() ? nullptr : &it->second;
    }

    [[nodiscard]] RACE* FindRaceById(uint32_t aFormId) noexcept
    {
        auto it = m_races.find(aFormId);
        return it == m_races.end() ? nullptr : &it.value();
    }

    [[nodiscard]] const LVLN* FindLeveledNpcById(uint32_t aFormId) const noexcept
    {
        const auto it = m_leveledNpcs.find(aFormId);
        return it == m_leveledNpcs.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const LVLI* FindLeveledItemById(uint32_t aFormId) const noexcept
    {
        const auto it = m_leveledItems.find(aFormId);
        return it == m_leveledItems.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const ECZN* FindEncounterZoneById(uint32_t aFormId) const noexcept
    {
        const auto it = m_encounterZones.find(aFormId);
        return it == m_encounterZones.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const CELL* FindCellById(uint32_t aFormId) const noexcept
    {
        const auto it = m_cells.find(aFormId);
        return it == m_cells.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const GLOB* FindGlobalById(uint32_t aFormId) const noexcept
    {
        const auto it = m_globals.find(aFormId);
        return it == m_globals.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const LCTN* FindLocationById(uint32_t aFormId) const noexcept
    {
        const auto it = m_locations.find(aFormId);
        return it == m_locations.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const REFR* FindObjectRefById(uint32_t aFormId) const noexcept
    {
        const auto it = m_objectReferences.find(aFormId);
        return it == m_objectReferences.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const CONT* FindContainerById(uint32_t aFormId) const noexcept
    {
        const auto it = m_containers.find(aFormId);
        return it == m_containers.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const Map<uint32_t, REFR>& GetObjectReferences() const noexcept { return m_objectReferences; }
    [[nodiscard]] const Map<uint32_t, ECZN>& GetEncounterZones() const noexcept { return m_encounterZones; }
    [[nodiscard]] const Map<uint32_t, LCTN>& GetLocations() const noexcept { return m_locations; }

    CONT& GetContainerById(uint32_t aFormId) noexcept { return m_containers[aFormId]; }
    GMST& GetGameSettingById(uint32_t aFormId) noexcept { return m_gameSettings[aFormId]; }
    WRLD& GetWorldById(uint32_t aFormId) noexcept { return m_worlds[aFormId]; }
    NAVM& GetNavMeshById(uint32_t aFormId) noexcept { return m_navMeshes[aFormId]; }

    void BuildReferences();

private:
    Map<uint32_t, Record> m_allRecords{};
    Map<uint32_t, ACHR> m_actorReferences{};
    Map<uint32_t, REFR> m_objectReferences{};
    Map<uint32_t, CLMT> m_climates{};
    Map<uint32_t, NPC> m_npcs{};
    Map<uint32_t, RACE> m_races{};
    Map<uint32_t, LVLN> m_leveledNpcs{};
    Map<uint32_t, CONT> m_containers{};
    Map<uint32_t, LVLI> m_leveledItems{};
    Map<uint32_t, ECZN> m_encounterZones{};
    Map<uint32_t, LCTN> m_locations{};
    Map<uint32_t, GLOB> m_globals{};
    Map<uint32_t, CELL> m_cells{};
    Map<uint32_t, GMST> m_gameSettings{};
    Map<uint32_t, WRLD> m_worlds{};
    Map<uint32_t, NAVM> m_navMeshes{};
};

} // namespace ESLoader
