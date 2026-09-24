#include <Structs/CharacterSummary.h>

#include <TiltedCore/Serialization.hpp>

void CharacterSummary::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, CharacterId);
    TiltedPhoques::Serialization::WriteString(aWriter, Name);
    Race.Serialize(aWriter);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, static_cast<std::uint32_t>(Sex));
    TiltedPhoques::Serialization::WriteVarInt(aWriter, static_cast<std::uint32_t>(Level));
    TiltedPhoques::Serialization::WriteVarInt(aWriter, SlotIndex);
}

void CharacterSummary::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    CharacterId = TiltedPhoques::Serialization::ReadVarInt(aReader);
    Name = TiltedPhoques::Serialization::ReadString(aReader);
    Race.Deserialize(aReader);
    Sex = static_cast<std::int32_t>(TiltedPhoques::Serialization::ReadVarInt(aReader));
    Level = static_cast<std::int32_t>(TiltedPhoques::Serialization::ReadVarInt(aReader));
    SlotIndex = static_cast<std::uint8_t>(TiltedPhoques::Serialization::ReadVarInt(aReader));
}

bool CharacterSummary::operator==(const CharacterSummary& acRhs) const noexcept
{
    return CharacterId == acRhs.CharacterId && Name == acRhs.Name && Race == acRhs.Race && Sex == acRhs.Sex && Level == acRhs.Level && SlotIndex == acRhs.SlotIndex;
}
