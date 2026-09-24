#include <Messages/NotifyObjectHarvested.h>

void NotifyObjectHarvested::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
    Serialization::WriteBool(aWriter, IsHarvested);
}

void NotifyObjectHarvested::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Id.Deserialize(aReader);
    IsHarvested = Serialization::ReadBool(aReader);
}
