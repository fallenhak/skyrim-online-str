#include <Structs/ReferenceUpdate.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>
#include <stdexcept>

using TiltedPhoques::Serialization;

bool ReferenceUpdate::operator==(const ReferenceUpdate& acRhs) const noexcept
{
    return OwnershipEpoch == acRhs.OwnershipEpoch && UpdatedMovement == acRhs.UpdatedMovement && ActionEvents == acRhs.ActionEvents;
}

bool ReferenceUpdate::operator!=(const ReferenceUpdate& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ReferenceUpdate::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);
    UpdatedMovement.Serialize(aWriter);

    const auto actionCount = std::min(ActionEvents.size(), MovementPayloadLimits::kMaxActionEvents);
    Serialization::WriteVarInt(aWriter, actionCount);

    for (auto it = ActionEvents.begin(); it != ActionEvents.begin() + actionCount; ++it)
    {
        it->GenerateDifferential(ActionEvent{}, aWriter);
    }
}

bool ReferenceUpdate::Deserialize(TiltedPhoques::Buffer::Reader& aReader)
{
    OwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    if (!UpdatedMovement.Deserialize(aReader))
        return false;

    const auto count = Serialization::ReadVarInt(aReader);
    if (count > MovementPayloadLimits::kMaxActionEvents)
        return false;

    ActionEvents.resize(static_cast<size_t>(count));

    for (auto i = 0u; i < count; ++i)
    {
        if (!ActionEvents[i].ApplyDifferential(aReader))
            return false;
    }

    return true;
}
