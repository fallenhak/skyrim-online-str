#include <Messages/NotifyCharacterSelectionResult.h>

void NotifyCharacterSelectionResult::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, static_cast<std::uint8_t>(Status));
}

void NotifyCharacterSelectionResult::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    Status = static_cast<CharacterSelectionStatus>(Serialization::ReadVarInt(aReader) & 0xFF);
}
