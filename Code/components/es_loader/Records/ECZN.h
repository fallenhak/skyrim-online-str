#pragma once

#include "Chunks.h"
#include "Record.h"

// https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format/ECZN
// Encounter zone: the level range of a place (world-state plan, deleveled world).
class ECZN : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::ECZN;

    enum Flags : uint8_t
    {
        kNeverResets = 0x01,
        kMatchPcBelowMinimumLevel = 0x02,
        kDisableCombatBoundary = 0x04,
    };

    String m_editorId{};
    uint32_t m_owner{};
    uint32_t m_location{};
    int8_t m_rank{};
    int8_t m_minLevel{};
    uint8_t m_flags{};
    int8_t m_maxLevel{}; // 0: no upper bound

    bool ParseChunks(const uint8_t* apRecordData, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
