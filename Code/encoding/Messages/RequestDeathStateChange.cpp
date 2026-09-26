#include <Messages/RequestDeathStateChange.h>

void RequestDeathStateChange::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);
    Serialization::WriteBool(aWriter, IsDead);
    Serialization::WriteBool(aWriter, IsSettledPosition);
    if (IsSettledPosition)
        CorpseContents.Serialize(aWriter);
}

void RequestDeathStateChange::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    OwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    IsDead = Serialization::ReadBool(aReader);
    IsSettledPosition = Serialization::ReadBool(aReader);
    if (IsSettledPosition)
        CorpseContents.Deserialize(aReader);
}
