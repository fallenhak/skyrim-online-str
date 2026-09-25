#pragma once

#include "Chunks.h"
#include "Record.h"

// https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format/LVLN
class LVLN : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::LVLN;

    struct Entry
    {
        uint16_t Level{};
        uint32_t FormId{}; // an NPC_ or a nested LVLN; zero when unresolved
        uint16_t Count{};
    };

    String m_editorId{};
    uint8_t m_chanceNone{};
    uint8_t m_flags{}; // same bits as LVLI: 0x01 calculate from all levels, 0x02 for each item
    // Resolved LVLO entry references (NPC_ or nested LVLN). Unresolved entries are zero.
    Vector<uint32_t> m_entryIds{};
    // The same entries with level and count, for server-side picks at a fixed place level.
    Vector<Entry> m_entries{};

    bool ParseChunks(const uint8_t* apRecordData, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
