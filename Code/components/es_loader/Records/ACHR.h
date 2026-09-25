#pragma once

#include "Chunks.h"
#include "Record.h"

// Skyrim placed actor reference record. The NAME/base-object relationship is used by the
// actor population identity resolver; the cell and encounter zone give a placed actor
// its fixed level (world-state plan, deleveled world).
class ACHR : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::ACHR;

    Chunks::NAME m_baseObject{};
    uint32_t m_encounterZone{}; // XEZN
    uint32_t m_parentCell{};    // set by TESFile from the enclosing cell group

    bool ParseChunks(const uint8_t* apRecordData, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
