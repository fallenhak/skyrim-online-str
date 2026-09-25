#pragma once

#include "Chunks.h"
#include "Record.h"

// https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format/CELL
// Only what places a reference in an encounter zone.
class CELL : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::CELL;

    uint32_t m_encounterZone{}; // XEZN
    uint32_t m_location{};      // XLCN

    bool ParseChunks(const uint8_t* apRecordData, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
