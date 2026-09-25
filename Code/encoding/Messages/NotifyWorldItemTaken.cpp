#include <Messages/NotifyWorldItemTaken.h>

void NotifyWorldItemTaken::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
}

void NotifyWorldItemTaken::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    Id.Deserialize(aReader);
}
