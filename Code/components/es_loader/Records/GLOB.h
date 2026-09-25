#pragma once

#include "Chunks.h"
#include "Record.h"

// https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format/GLOB
// Global variable. Read for LVLI's LVLG: its value overrides a list's chance none.
class GLOB : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::GLOB;

    String m_editorId{};
    float m_value{}; // FLTV; short and long globals are stored as a float too

    bool ParseChunks(const uint8_t* apRecordData, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
