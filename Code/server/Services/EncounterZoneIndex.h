#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace ESLoader
{
struct RecordCollection;
}

// Deleveled world (world-state plan, phase 1a): the level of a placed reference comes
// from its encounter zone, never from a player. A zone is found, in order, from the
// reference's own XEZN, its cell's XEZN, or the ECZN tied to the cell's location or
// one of that location's parents.
class EncounterZoneIndex
{
public:
    struct LevelRange
    {
        int32_t Min{};
        int32_t Max{}; // 0: no upper bound

        bool operator==(const LevelRange& acRhs) const noexcept { return Min == acRhs.Min && Max == acRhs.Max; }
    };

    enum class Source : uint8_t
    {
        kNone,
        kReference,
        kCell,
        kLocation,
    };

    struct Resolution
    {
        uint32_t ZoneId{};
        Source From{Source::kNone};
    };

    // Plain inputs so the rules are testable without plugin files.
    struct ReferenceInput
    {
        uint32_t ReferenceZone{};
        uint32_t CellZone{};
        uint32_t CellLocation{};
    };

    static constexpr uint32_t kMaxLocationDepth = 32;

    EncounterZoneIndex() = default;

    void AddZone(uint32_t aZoneId, uint32_t aLocationId, LevelRange aRange);
    void AddLocationParent(uint32_t aLocationId, uint32_t aParentId);

    [[nodiscard]] Resolution Resolve(const ReferenceInput& acInput) const noexcept;
    [[nodiscard]] std::optional<LevelRange> FindRange(uint32_t aZoneId) const noexcept;

    // Builds the index from loaded plugins and logs coverage of container references.
    [[nodiscard]] static EncounterZoneIndex Build(const ESLoader::RecordCollection& acRecords) noexcept;
    [[nodiscard]] Resolution ResolveReference(const ESLoader::RecordCollection& acRecords, uint32_t aReferenceId) const noexcept;

    [[nodiscard]] size_t ZoneCount() const noexcept { return m_ranges.size(); }

private:
    [[nodiscard]] uint32_t ZoneForLocation(uint32_t aLocationId) const noexcept;

    std::unordered_map<uint32_t, LevelRange> m_ranges;
    // First zone registered for a location wins, like the plugin order that produced it.
    std::unordered_map<uint32_t, uint32_t> m_zoneByLocation;
    std::unordered_map<uint32_t, uint32_t> m_locationParent;
};

inline void EncounterZoneIndex::AddZone(const uint32_t aZoneId, const uint32_t aLocationId, const LevelRange aRange)
{
    m_ranges[aZoneId] = aRange;
    if (aLocationId)
        m_zoneByLocation.emplace(aLocationId, aZoneId);
}

inline void EncounterZoneIndex::AddLocationParent(const uint32_t aLocationId, const uint32_t aParentId)
{
    if (aLocationId && aParentId && aLocationId != aParentId)
        m_locationParent[aLocationId] = aParentId;
}

inline uint32_t EncounterZoneIndex::ZoneForLocation(uint32_t aLocationId) const noexcept
{
    // Bounded walk: a malformed plugin can make the parent chain cyclic.
    for (uint32_t depth = 0; aLocationId && depth < kMaxLocationDepth; ++depth)
    {
        if (const auto it = m_zoneByLocation.find(aLocationId); it != m_zoneByLocation.end())
            return it->second;

        const auto parent = m_locationParent.find(aLocationId);
        aLocationId = parent == m_locationParent.end() ? 0 : parent->second;
    }
    return 0;
}

inline EncounterZoneIndex::Resolution EncounterZoneIndex::Resolve(const ReferenceInput& acInput) const noexcept
{
    if (acInput.ReferenceZone && m_ranges.contains(acInput.ReferenceZone))
        return {acInput.ReferenceZone, Source::kReference};
    if (acInput.CellZone && m_ranges.contains(acInput.CellZone))
        return {acInput.CellZone, Source::kCell};
    if (const uint32_t zone = ZoneForLocation(acInput.CellLocation))
        return {zone, Source::kLocation};
    return {};
}

inline std::optional<EncounterZoneIndex::LevelRange> EncounterZoneIndex::FindRange(const uint32_t aZoneId) const noexcept
{
    const auto it = m_ranges.find(aZoneId);
    if (it == m_ranges.end())
        return std::nullopt;
    return it->second;
}
