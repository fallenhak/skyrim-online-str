#pragma once

#include "Message.h"
#include <Structs/Factions.h>

struct RequestFactionsChanges final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestFactionsChanges;

    RequestFactionsChanges()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestFactionsChanges() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestFactionsChanges& acRhs) const noexcept { return Changes == acRhs.Changes && GetOpcode() == acRhs.GetOpcode(); }

    TiltedPhoques::Map<uint32_t, FactionUpdate> Changes;
    // Wire count refused by the read bound; 0 when the list was read. Not serialized: the
    // server logs and drops such a message instead of handling it as an empty list.
    uint64_t OverLimitCount{};
};
