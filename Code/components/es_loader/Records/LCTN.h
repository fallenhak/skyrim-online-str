#pragma once

#include "Chunks.h"
#include "Record.h"

// https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format/LCTN
// Only the parent chain: an encounter zone set on a location also covers its children.
class LCTN : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::LCTN;

    uint32_t m_parentLocation{};

    bool ParseChunks(const uint8_t* apRecordData, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
