#include <Messages/NotifyActorMaxValueChanges.h>

namespace
{
constexpr uint64_t kMaxActorValueChangeCount = 256;
}

void NotifyActorMaxValueChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);

    Serialization::WriteVarInt(aWriter, Values.size());
    for (auto& value : Values)
    {
        Serialization::WriteVarInt(aWriter, value.first);
        Serialization::WriteFloat(aWriter, value.second);
    }
}

void NotifyActorMaxValueChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    OwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    const auto count = Serialization::ReadVarInt(aReader);
    if (count > kMaxActorValueChangeCount)
        return;

    for (uint64_t i = 0; i < count; i++)
    {
        const uint32_t key = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
        auto value = Serialization::ReadFloat(aReader);
        Values.insert({key, value});
    }
}
