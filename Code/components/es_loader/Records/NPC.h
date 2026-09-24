#pragma once

#include "Record.h"
#include "Chunks.h"

// https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format/NPC
class NPC : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::NPC_;

    String m_editorId = "";
    uint32_t m_raceId{};
    // Resolved TPLT target (NPC_ or LVLN); zero when absent or unresolved.
    uint32_t m_templateId{};
    // ACBS template data flags; Chunks::ACBS::kTraits means the race comes from m_templateId.
    uint16_t m_templateDataFlags{};
    Chunks::ACBS m_baseStats{};
    Chunks::DOFT m_defaultOutfit{};
    Chunks::VMAD m_scriptData{};

    bool ParseChunks(const uint8_t* apRecordData, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
