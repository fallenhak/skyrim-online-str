#include <Services/EncounterZoneIndex.h>

#include <es_loader/RecordCollection.h>

EncounterZoneIndex::Resolution EncounterZoneIndex::ResolveReference(const ESLoader::RecordCollection& acRecords, const uint32_t aReferenceId) const noexcept
{
    const REFR* pReference = acRecords.FindObjectRefById(aReferenceId);
    if (!pReference)
        return {};

    ReferenceInput input{};
    input.ReferenceZone = pReference->m_encounterZone;
    if (const CELL* pCell = acRecords.FindCellById(pReference->m_parentCell))
    {
        input.CellZone = pCell->m_encounterZone;
        input.CellLocation = pCell->m_location;
    }
    return Resolve(input);
}

EncounterZoneIndex EncounterZoneIndex::Build(const ESLoader::RecordCollection& acRecords) noexcept
{
    EncounterZoneIndex index;
    for (const auto& [id, zone] : acRecords.GetEncounterZones())
        index.AddZone(id, zone.m_location, {zone.m_minLevel, zone.m_maxLevel});
    for (const auto& [id, location] : acRecords.GetLocations())
        index.AddLocationParent(id, location.m_parentLocation);

    // Coverage report: how many placed containers get a fixed level from the plugins.
    size_t containers = 0;
    size_t bySource[4]{};
    for (const auto& [id, reference] : acRecords.GetObjectReferences())
    {
        if (!acRecords.FindContainerById(reference.m_basicObject.m_baseId))
            continue;
        ++containers;
        ++bySource[static_cast<size_t>(index.ResolveReference(acRecords, id).From)];
    }

    spdlog::info(
        "[World] encounter zones: {} zone(s); {} container reference(s): {} by reference, {} by cell, {} by location, {} without a zone",
        index.ZoneCount(), containers, bySource[static_cast<size_t>(Source::kReference)], bySource[static_cast<size_t>(Source::kCell)],
        bySource[static_cast<size_t>(Source::kLocation)], bySource[static_cast<size_t>(Source::kNone)]);
    return index;
}
