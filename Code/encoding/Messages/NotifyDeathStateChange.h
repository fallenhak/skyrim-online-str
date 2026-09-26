#pragma once

#include "Message.h"
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/Inventory.h>

struct NotifyDeathStateChange final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyDeathStateChange;

    NotifyDeathStateChange()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyDeathStateChange& acRhs) const noexcept { return Id == acRhs.Id && OwnershipEpoch == acRhs.OwnershipEpoch && IsDead == acRhs.IsDead && IsSettledPosition == acRhs.IsSettledPosition && (!IsSettledPosition || (Position == acRhs.Position && HasCorpseContents == acRhs.HasCorpseContents && CorpseContents == acRhs.CorpseContents)) && GetOpcode() == acRhs.GetOpcode(); }

    uint32_t Id;
    uint32_t OwnershipEpoch{};
    bool IsDead;
    bool IsSettledPosition{};
    Vector3_NetQuantize Position{};
    // The corpse's recorded contents, replacing the death items this client rolled itself.
    bool HasCorpseContents{};
    Inventory CorpseContents{};
};
