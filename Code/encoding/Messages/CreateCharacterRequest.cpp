#include <Messages/CreateCharacterRequest.h>

#include <TiltedCore/Serialization.hpp>

void CreateCharacterRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, SlotIndex);
    TiltedPhoques::Serialization::WriteString(aWriter, Name);
}

void CreateCharacterRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    SlotIndex = static_cast<std::uint32_t>(TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu);
    Name = TiltedPhoques::Serialization::ReadString(aReader);
}
