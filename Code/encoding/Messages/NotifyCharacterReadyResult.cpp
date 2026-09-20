#include <Messages/NotifyCharacterReadyResult.h>

#include <TiltedCore/Serialization.hpp>

void NotifyCharacterReadyResult::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, static_cast<std::uint8_t>(Status));
}

void NotifyCharacterReadyResult::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    Status = static_cast<CharacterReadyStatus>(Serialization::ReadVarInt(aReader) & 0xFF);
}
