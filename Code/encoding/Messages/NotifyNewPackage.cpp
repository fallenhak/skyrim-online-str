#include <Messages/NotifyNewPackage.h>

void NotifyNewPackage::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ActorId);
    PackageId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);
}

void NotifyNewPackage::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ActorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    PackageId.Deserialize(aReader);
    OwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
