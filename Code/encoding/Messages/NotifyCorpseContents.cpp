#include <Messages/NotifyCorpseContents.h>
#include <TiltedCore/Serialization.hpp>

void NotifyCorpseContents::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Contents.Serialize(aWriter);
}

void NotifyCorpseContents::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Contents.Deserialize(aReader);
}
