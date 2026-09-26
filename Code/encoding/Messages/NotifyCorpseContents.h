#pragma once

#include "Message.h"

#include <Structs/Inventory.h>

// The server's contents of a corpse, sent to a client whose state report showed that corpse with
// different contents: a take relayed while it was not loaded, or a notification it could not apply.
struct NotifyCorpseContents final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCorpseContents;

    NotifyCorpseContents()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyCorpseContents& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && Contents == acRhs.Contents;
    }

    uint32_t ServerId{};
    Inventory Contents{};
};
