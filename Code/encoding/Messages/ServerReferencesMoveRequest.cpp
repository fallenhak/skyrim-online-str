#include <Messages/ServerReferencesMoveRequest.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

void ServerReferencesMoveRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    const auto updateCount = std::min(Updates.size(), kMaxUpdates);
    Serialization::WriteVarInt(aWriter, updateCount);

    std::size_t written = 0;
    for (const auto& kvp : Updates)
    {
        if (written++ >= updateCount)
            break;

        Serialization::WriteVarInt(aWriter, kvp.first);
        kvp.second.Serialize(aWriter);
    }
}

void ServerReferencesMoveRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Tick = Serialization::ReadVarInt(aReader);
    const auto count = Serialization::ReadVarInt(aReader);
    Updates.clear();

    if (count > kMaxUpdates)
        return;

    for (auto i = 0u; i < count; ++i)
    {
        const uint32_t cServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        auto& update = Updates[cServerId];
        if (!update.Deserialize(aReader))
        {
            Updates.clear();
            return;
        }
    }
}
