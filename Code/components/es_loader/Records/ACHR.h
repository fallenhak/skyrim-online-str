#pragma once

#include "Chunks.h"
#include "Record.h"

// Skyrim placed actor reference record. Only the NAME/base-object relationship is
// needed by the actor population identity resolver.
class ACHR : public Record
{
public:
    static constexpr FormEnum kType = FormEnum::ACHR;

    Chunks::NAME m_baseObject{};

    bool ParseChunks(const uint8_t* apRecordData, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;
};
