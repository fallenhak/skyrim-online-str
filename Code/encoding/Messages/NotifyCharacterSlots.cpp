#include <Messages/NotifyCharacterSlots.h>

#include <TiltedCore/Serialization.hpp>

void NotifyCharacterSlots::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Total);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Unlocked);
}

void NotifyCharacterSlots::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    Total = static_cast<std::uint32_t>(TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu);
    Unlocked = static_cast<std::uint32_t>(TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu);
}
