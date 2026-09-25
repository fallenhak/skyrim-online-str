#pragma once

#include "Record.h"
#include "Chunks.h"

class REFR : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::REFR;

    Chunks::NAME m_basicObject{};
    Chunks::MapMarkerData m_markerData{};
    // The CELL whose children group holds this reference (set by TESFile).
    uint32_t m_parentCell{};
    // XLOC: present when the reference starts locked.
    bool m_isLocked{};
    uint8_t m_lockLevel{};
    uint32_t m_lockKey{};
    // XEZN on the reference itself overrides the cell's encounter zone.
    uint32_t m_encounterZone{};

    void ParseChunks(REFR& aSourceRecord, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
