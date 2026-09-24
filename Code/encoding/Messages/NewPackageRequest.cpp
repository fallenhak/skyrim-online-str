#include <Messages/NewPackageRequest.h>

void NewPackageRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ActorId);
    PackageId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);
}

void NewPackageRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    ActorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    PackageId.Deserialize(aReader);
    OwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
