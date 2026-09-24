#include <Messages/SelectCharacterRequest.h>

void SelectCharacterRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, CharacterId);
}

void SelectCharacterRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    CharacterId = Serialization::ReadVarInt(aReader);
}
