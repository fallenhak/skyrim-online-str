#include <Services/PluginContainerContents.h>
#include <Services/EncounterZoneIndex.h>

#include <Components.h>
#include <es_loader/RecordCollection.h>

#include <algorithm>

PluginContainerContents::PluginContainerContents(const ESLoader::RecordCollection* apRecords, const EncounterZoneIndex& acZones) noexcept
    : m_pRecords(apRecords)
    , m_zones(acZones)
{
}

const LeveledItemResolver::List* PluginContainerContents::FindList(const uint32_t aFormId) noexcept
{
    if (const auto it = m_listCache.find(aFormId); it != m_listCache.end())
        return &it->second;

    const LVLI* pRecord = m_pRecords->FindLeveledItemById(aFormId);
    if (!pRecord)
        return nullptr;

    // LVLG (a global overriding chance none) is not read yet; the record's own value is used.
    LeveledItemResolver::List list{pRecord->m_chanceNone, pRecord->m_flags, {}};
    list.Entries.reserve(pRecord->m_entries.size());
    for (const auto& entry : pRecord->m_entries)
        list.Entries.push_back({entry.Level, entry.FormId, entry.Count});

    return &m_listCache.emplace(aFormId, std::move(list)).first->second;
}

std::optional<PluginContainerContents::Result> PluginContainerContents::Build(const GameId& acReferenceId, const ModsComponent& acMods) noexcept
{
    if (!m_pRecords)
        return std::nullopt;

    uint32_t referenceFormId = 0;
    if (!acMods.ResolveServerFormId(acReferenceId, referenceFormId))
        return std::nullopt;

    const REFR* pReference = m_pRecords->FindObjectRefById(referenceFormId);
    if (!pReference)
        return std::nullopt;
    const CONT* pContainer = m_pRecords->FindContainerById(pReference->m_basicObject.m_baseId);
    if (!pContainer)
        return std::nullopt;

    Result result{};
    const auto zone = m_zones.ResolveReference(*m_pRecords, referenceFormId);
    result.ZoneId = zone.ZoneId;
    result.Level = kDefaultPlaceLevel;
    if (const auto range = m_zones.FindRange(zone.ZoneId))
        result.Level = static_cast<uint16_t>(std::max<int32_t>(1, range->Min));

    const auto lookup = [this](uint32_t aFormId) { return FindList(aFormId); };

    std::vector<LeveledItemResolver::Item> items;
    uint32_t objectIndex = 0;
    for (const auto& object : pContainer->m_objects)
    {
        ++objectIndex;
        if (object.m_formId == 0 || object.m_count == 0)
            continue;

        if (const auto* pList = FindList(object.m_formId))
        {
            // Seeded by reference and slot: a restart before persistence gives the same roll.
            const auto rolled = LeveledItemResolver::Resolve(*pList, result.Level, object.m_count, lookup, referenceFormId * 31u + objectIndex);
            items.insert(items.end(), rolled.begin(), rolled.end());
        }
        else
            items.push_back({object.m_formId, static_cast<int32_t>(object.m_count)});
    }

    for (const auto& item : items)
    {
        Inventory::Entry entry{};
        if (!acMods.ToNetworkId(item.FormId, entry.BaseId))
            return std::nullopt;
        entry.Count = item.Count;
        result.Contents.AddOrRemoveEntry(entry);
    }

    return result;
}
