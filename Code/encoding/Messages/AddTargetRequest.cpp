#include <Messages/AddTargetRequest.h>

void AddTargetRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, TargetId);
    Serialization::WriteVarInt(aWriter, CasterId);
    SpellId.Serialize(aWriter);
    EffectId.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Magnitude);
    Serialization::WriteBool(aWriter, IsDualCasting);
    Serialization::WriteBool(aWriter, ApplyHealPerkBonus);
    Serialization::WriteBool(aWriter, ApplyStaminaPerkBonus);
    Serialization::WriteVarInt(aWriter, TargetOwnershipEpoch);
    Serialization::WriteVarInt(aWriter, CasterOwnershipEpoch);
}

void AddTargetRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    TargetId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CasterId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    SpellId.Deserialize(aReader);
    EffectId.Deserialize(aReader);
    Magnitude = Serialization::ReadFloat(aReader);
    IsDualCasting = Serialization::ReadBool(aReader);
    ApplyHealPerkBonus = Serialization::ReadBool(aReader);
    ApplyStaminaPerkBonus = Serialization::ReadBool(aReader);
    TargetOwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CasterOwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
