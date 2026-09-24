#include <Messages/NotifyCharacterCreateResult.h>

#include <TiltedCore/Serialization.hpp>

void NotifyCharacterCreateResult::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, static_cast<std::uint32_t>(Status));
    TiltedPhoques::Serialization::WriteVarInt(aWriter, CharacterId);
}

void NotifyCharacterCreateResult::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    Status = static_cast<CharacterCreateStatus>(TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFu);
    CharacterId = TiltedPhoques::Serialization::ReadVarInt(aReader);
}
