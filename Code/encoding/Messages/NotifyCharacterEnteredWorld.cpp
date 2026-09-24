#include <Messages/NotifyCharacterEnteredWorld.h>

#include <TiltedCore/Serialization.hpp>

void NotifyCharacterEnteredWorld::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, CharacterId);
}

void NotifyCharacterEnteredWorld::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    CharacterId = Serialization::ReadVarInt(aReader);
}
