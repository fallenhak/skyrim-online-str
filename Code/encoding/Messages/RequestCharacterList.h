#pragma once

#include "Message.h"

struct RequestCharacterList final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestCharacterList;

    RequestCharacterList()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer&) const noexcept override {}
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { ClientMessage::DeserializeRaw(aReader); }

    bool operator==(const RequestCharacterList& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode(); }
};
