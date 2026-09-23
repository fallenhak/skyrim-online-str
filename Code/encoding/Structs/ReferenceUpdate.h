#pragma once

#include <Structs/Movement.h>
#include <Structs/ActionEvent.h>
#include <Structs/MovementPayloadLimits.h>

#include <cstdint>

using TiltedPhoques::Buffer;
using TiltedPhoques::Vector;

struct ReferenceUpdate
{
    ReferenceUpdate() = default;
    ~ReferenceUpdate() = default;

    bool operator==(const ReferenceUpdate& acRhs) const noexcept;
    bool operator!=(const ReferenceUpdate& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader& aReader);

    uint32_t OwnershipEpoch{};
    Movement UpdatedMovement{};
    Vector<ActionEvent> ActionEvents{};
};
