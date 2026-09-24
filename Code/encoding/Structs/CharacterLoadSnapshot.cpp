#include <Structs/CharacterLoadSnapshot.h>

#include <TiltedCore/Serialization.hpp>

void CharacterLoadSnapshot::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, CharacterId);
    TiltedPhoques::Serialization::WriteString(aWriter, Name);
    Race.Serialize(aWriter);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, static_cast<std::uint32_t>(Sex));
    TiltedPhoques::Serialization::WriteVarInt(aWriter, static_cast<std::uint32_t>(Level));
    WorldSpaceId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    TiltedPhoques::Serialization::WriteFloat(aWriter, PositionX);
    TiltedPhoques::Serialization::WriteFloat(aWriter, PositionY);
    TiltedPhoques::Serialization::WriteFloat(aWriter, PositionZ);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Health);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Magicka);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Stamina);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, NeedsRaceMenu ? 1u : 0u);
}

void CharacterLoadSnapshot::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    CharacterId = TiltedPhoques::Serialization::ReadVarInt(aReader);
    Name = TiltedPhoques::Serialization::ReadString(aReader);
    Race.Deserialize(aReader);
    Sex = static_cast<std::int32_t>(TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);
    Level = static_cast<std::int32_t>(TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);
    WorldSpaceId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    PositionX = TiltedPhoques::Serialization::ReadFloat(aReader);
    PositionY = TiltedPhoques::Serialization::ReadFloat(aReader);
    PositionZ = TiltedPhoques::Serialization::ReadFloat(aReader);
    Health = TiltedPhoques::Serialization::ReadFloat(aReader);
    Magicka = TiltedPhoques::Serialization::ReadFloat(aReader);
    Stamina = TiltedPhoques::Serialization::ReadFloat(aReader);
    NeedsRaceMenu = TiltedPhoques::Serialization::ReadVarInt(aReader) != 0;
}

bool CharacterLoadSnapshot::operator==(const CharacterLoadSnapshot& acRhs) const noexcept
{
    return CharacterId == acRhs.CharacterId && Name == acRhs.Name && Race == acRhs.Race && Sex == acRhs.Sex && Level == acRhs.Level && WorldSpaceId == acRhs.WorldSpaceId && CellId == acRhs.CellId &&
           PositionX == acRhs.PositionX && PositionY == acRhs.PositionY && PositionZ == acRhs.PositionZ && Health == acRhs.Health && Magicka == acRhs.Magicka && Stamina == acRhs.Stamina &&
           NeedsRaceMenu == acRhs.NeedsRaceMenu;
}
