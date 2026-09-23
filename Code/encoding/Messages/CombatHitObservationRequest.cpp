#include <Messages/CombatHitObservationRequest.h>

void CombatHitObservationRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, AttackerServerId);
    Serialization::WriteVarInt(aWriter, AttackerOwnershipEpoch);
    Serialization::WriteVarInt(aWriter, TargetServerId);
    Serialization::WriteVarInt(aWriter, TargetLifecycleGeneration);
    Serialization::WriteVarInt(aWriter, ObservationId);
}

void CombatHitObservationRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    AttackerServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    AttackerOwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    TargetServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    TargetLifecycleGeneration = Serialization::ReadVarInt(aReader);
    ObservationId = Serialization::ReadVarInt(aReader);
}
