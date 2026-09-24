#pragma once

#include "Chunks.h"
#include "Record.h"

class RACE : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::RACE;

    String m_editorId{};

    void ParseChunks(RACE& aSourceRecord, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
