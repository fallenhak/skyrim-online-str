#include <Messages/AuthenticationRequest.h>

void AuthenticationRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, DiscordId);
    Serialization::WriteBool(aWriter, SKSEActive);
    Serialization::WriteBool(aWriter, MO2Active);
    Serialization::WriteString(aWriter, Token);
    Serialization::WriteString(aWriter, Version);
    UserMods.Serialize(aWriter);
    Serialization::WriteString(aWriter, Username);
    Serialization::WriteVarInt(aWriter, RaceFormId);
    Serialization::WriteVarInt(aWriter, Sex);
    Position.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, WorldSpaceFormId);
    Serialization::WriteVarInt(aWriter, CellFormId);
    WorldSpaceId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Level);
    PlayerTime.Serialize(aWriter);
}

void AuthenticationRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    DiscordId = Serialization::ReadVarInt(aReader);
    SKSEActive = Serialization::ReadBool(aReader);
    MO2Active = Serialization::ReadBool(aReader);
    Token = Serialization::ReadString(aReader);
    Version = Serialization::ReadString(aReader);
    UserMods.Deserialize(aReader);
    Username = Serialization::ReadString(aReader);
    RaceFormId = static_cast<std::uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu);
    Sex = static_cast<std::uint8_t>(Serialization::ReadVarInt(aReader) & 0xFFu);
    Position.Deserialize(aReader);
    WorldSpaceFormId = static_cast<std::uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu);
    CellFormId = static_cast<std::uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu);
    WorldSpaceId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    Level = Serialization::ReadVarInt(aReader) & 0xFFFF;
    PlayerTime.Deserialize(aReader);
}
