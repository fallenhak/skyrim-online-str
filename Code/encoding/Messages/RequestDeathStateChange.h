#pragma once

#include "Message.h"

#include <Structs/Inventory.h>

struct RequestDeathStateChange final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestDeathStateChange;

    RequestDeathStateChange()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestDeathStateChange& acRhs) const noexcept { return Id == acRhs.Id && OwnershipEpoch == acRhs.OwnershipEpoch && IsDead == acRhs.IsDead && IsSettledPosition == acRhs.IsSettledPosition && (!IsSettledPosition || CorpseContents == acRhs.CorpseContents) && GetOpcode() == acRhs.GetOpcode(); }

    uint32_t Id;
    uint32_t OwnershipEpoch{};
    bool IsDead;
    bool IsSettledPosition{};
    // With the settled position: the owner's corpse contents, including what the engine added at
    // death. Every other client rolled its own death items; this one becomes the corpse's record.
    Inventory CorpseContents{};
};
