#include <Messages/NotifyDeathStateChange.h>

void NotifyDeathStateChange::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);
    Serialization::WriteBool(aWriter, IsDead);
    Serialization::WriteBool(aWriter, IsSettledPosition);
    if (IsSettledPosition)
    {
        Position.Serialize(aWriter);
        Serialization::WriteBool(aWriter, HasCorpseContents);
        if (HasCorpseContents)
            CorpseContents.Serialize(aWriter);
    }
}

void NotifyDeathStateChange::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    OwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    IsDead = Serialization::ReadBool(aReader);
    IsSettledPosition = Serialization::ReadBool(aReader);
    if (IsSettledPosition)
    {
        Position.Deserialize(aReader);
        HasCorpseContents = Serialization::ReadBool(aReader);
        if (HasCorpseContents)
            CorpseContents.Deserialize(aReader);
    }
}
