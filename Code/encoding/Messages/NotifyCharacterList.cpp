#include <Messages/NotifyCharacterList.h>

void NotifyCharacterList::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Characters.size());
    for (const auto& character : Characters)
        character.Serialize(aWriter);
}

void NotifyCharacterList::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    const auto count = Serialization::ReadVarInt(aReader) & 0xFFFF;
    Characters.clear();
    Characters.reserve(static_cast<std::size_t>(count));
    for (auto i = 0u; i < count; ++i)
    {
        Characters.emplace_back().Deserialize(aReader);
    }
}
