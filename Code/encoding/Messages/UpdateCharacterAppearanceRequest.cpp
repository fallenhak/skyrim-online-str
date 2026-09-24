#include <Messages/UpdateCharacterAppearanceRequest.h>

#include <TiltedCore/Serialization.hpp>

void UpdateCharacterAppearanceRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Race.Serialize(aWriter);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, static_cast<std::uint32_t>(Sex));
}

void UpdateCharacterAppearanceRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    Race.Deserialize(aReader);
    Sex = static_cast<std::int32_t>(TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu);
}
