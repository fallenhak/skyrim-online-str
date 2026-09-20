#include <Messages/NotifyProgressionAward.h>

#include <TiltedCore/Serialization.hpp>

void NotifyProgressionAward::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, AwardId);
    Serialization::WriteVarInt(aWriter, CharacterId);
    Serialization::WriteVarInt(aWriter, static_cast<std::uint8_t>(Skill));
    Serialization::WriteFloat(aWriter, Experience);
    Serialization::WriteVarInt(aWriter, static_cast<std::uint8_t>(Reason));
}

void NotifyProgressionAward::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    AwardId = Serialization::ReadVarInt(aReader);
    CharacterId = Serialization::ReadVarInt(aReader);
    Skill = static_cast<ProgressionSkill>(Serialization::ReadVarInt(aReader));
    Experience = Serialization::ReadFloat(aReader);
    Reason = static_cast<ProgressionAwardReason>(Serialization::ReadVarInt(aReader));
}
