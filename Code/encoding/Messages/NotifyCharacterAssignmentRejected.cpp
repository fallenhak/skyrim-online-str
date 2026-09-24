#include <Messages/NotifyCharacterAssignmentRejected.h>

#include <TiltedCore/Serialization.hpp>

void NotifyCharacterAssignmentRejected::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Cookie);
    Serialization::WriteVarInt(aWriter, static_cast<std::uint8_t>(Reason));
}

void NotifyCharacterAssignmentRejected::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    Cookie = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Reason = static_cast<CharacterAssignmentRejectReason>(Serialization::ReadVarInt(aReader) & 0xFF);
}
