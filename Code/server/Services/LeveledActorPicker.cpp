#include <Services/LeveledActorPicker.h>
#include <Services/EncounterZoneIndex.h>
#include <Services/PluginContainerContents.h>

#include <es_loader/RecordCollection.h>

#include <algorithm>

LeveledActorPicker::LeveledActorPicker(const ESLoader::RecordCollection* apRecords, const EncounterZoneIndex& acZones) noexcept
    : m_pRecords(apRecords)
    , m_zones(acZones)
{
}

const LeveledItemResolver::List* LeveledActorPicker::FindList(const uint32_t aFormId) noexcept
{
    if (const auto it = m_listCache.find(aFormId); it != m_listCache.end())
        return &it->second;

    const LVLN* pRecord = m_pRecords->FindLeveledNpcById(aFormId);
    if (!pRecord)
        return nullptr;

    // LVLN flags share the LVLI bits for "all levels" and "each item"; "use all" is not
    // defined for actors and is masked off.
    LeveledItemResolver::List list{pRecord->m_chanceNone, static_cast<uint8_t>(pRecord->m_flags & ~LeveledItemResolver::kUseAll), {}};
    list.Entries.reserve(pRecord->m_entries.size());
    for (const auto& entry : pRecord->m_entries)
        list.Entries.push_back({entry.Level, entry.FormId, entry.Count});

    return &m_listCache.emplace(aFormId, std::move(list)).first->second;
}

std::optional<LeveledActorPicker::Pick> LeveledActorPicker::PickForReference(const uint32_t aReferenceFormId) noexcept
{
    if (!m_pRecords)
        return std::nullopt;

    const ACHR* pActor = m_pRecords->FindActorReferenceById(aReferenceFormId);
    if (!pActor)
        return std::nullopt;

    const auto templateOf = [this](uint32_t aFormId) -> uint32_t
    {
        const NPC* pNpc = m_pRecords->FindNpcById(aFormId);
        return pNpc ? pNpc->m_templateId : 0;
    };
    const auto isLeveledList = [this](uint32_t aFormId) { return m_pRecords->FindLeveledNpcById(aFormId) != nullptr; };

    const uint32_t listId = FindLeveledTemplate(pActor->m_baseObject.m_baseId, templateOf, isLeveledList);
    const auto* pList = listId ? FindList(listId) : nullptr;
    if (!pList)
        return std::nullopt;

    Pick pick{};
    pick.LeveledListId = listId;
    const auto zone = m_zones.ResolveReference(*m_pRecords, aReferenceFormId);
    pick.ZoneId = zone.ZoneId;
    pick.Level = PluginContainerContents::kDefaultPlaceLevel;
    if (const auto range = m_zones.FindRange(zone.ZoneId))
        pick.Level = static_cast<uint16_t>(std::max<int32_t>(1, range->Min));

    const auto lookup = [this](uint32_t aFormId) { return FindList(aFormId); };
    // Seeded by the reference: every restart and every owner gets the same actor.
    pick.NpcFormId = PickNpc(*pList, pick.Level, lookup, aReferenceFormId * 31u);
    return pick;
}
