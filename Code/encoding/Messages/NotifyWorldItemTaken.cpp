#include <Messages/NotifyWorldItemTaken.h>

#include <TiltedCore/Serialization.hpp>

void NotifyWorldItemTaken::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
    TiltedPhoques::Serialization::WriteBool(aWriter, IsTaken);
}

void NotifyWorldItemTaken::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    Id.Deserialize(aReader);
    IsTaken = TiltedPhoques::Serialization::ReadBool(aReader);
}
