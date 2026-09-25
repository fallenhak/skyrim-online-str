#pragma once

#include "Chunks.h"
#include "Record.h"

// https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format/LVLI
// Read for server-side leveled item resolution (world-state plan, phase 1).
class LVLI : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::LVLI;

    enum Flags : uint8_t
    {
        kCalculateFromAllLevels = 0x01, // every entry at or below the level, not only the highest
        kCalculateForEachItem = 0x02,   // a count above one rolls the list again per item
        kUseAll = 0x04,                 // every eligible entry is given
        kSpecialLoot = 0x08,
    };

    struct Entry
    {
        uint16_t Level{};
        uint32_t FormId{}; // an item or a nested LVLI; zero when unresolved
        uint16_t Count{};
    };

    String m_editorId{};
    uint8_t m_chanceNone{};
    uint8_t m_flags{};
    uint32_t m_chanceNoneGlobal{}; // LVLG: a GLOB that overrides m_chanceNone
    Vector<Entry> m_entries{};

    bool ParseChunks(const uint8_t* apRecordData, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
