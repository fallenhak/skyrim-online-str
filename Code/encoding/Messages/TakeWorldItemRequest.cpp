#include <Messages/TakeWorldItemRequest.h>

void TakeWorldItemRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, ActivatorId);
}

void TakeWorldItemRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Id.Deserialize(aReader);
    CellId.Deserialize(aReader);
    ActivatorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
