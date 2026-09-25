#pragma once

#include "Message.h"
#include <Structs/ObjectStateDigest.h>

// Desync detector: the client's periodic view of the synced references around it.
struct ObjectStateReport final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kObjectStateReport;

    ObjectStateReport()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ObjectStateReport& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Objects == acRhs.Objects; }

    Vector<ObjectStateDigest> Objects{};
    // Not serialized: set when a bound was exceeded; the server drops such a report.
    bool IsMalformed{};
};
