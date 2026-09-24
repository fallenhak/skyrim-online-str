#pragma once

#include "Chunks.h"
#include "Record.h"

// https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format/LVLN
class LVLN : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::LVLN;

    String m_editorId{};
    // Resolved LVLO entry references (NPC_ or nested LVLN). Unresolved entries are zero.
    Vector<uint32_t> m_entryIds{};

    bool ParseChunks(const uint8_t* apRecordData, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
