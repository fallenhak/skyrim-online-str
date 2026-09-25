#pragma once

#include "Message.h"
#include <Structs/Movement.h>

// Sent only to the owner whose seat action lost the server-side occupancy race.
struct NotifyFurnitureUseDenied final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyFurnitureUseDenied;

    NotifyFurnitureUseDenied()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t ActorId{};
    uint32_t OwnershipEpoch{};
    Movement AuthoritativeMovement{};
};
