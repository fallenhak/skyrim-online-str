#include <Messages/AssignObjectsResponse.h>

namespace
{
// Mirrors AssignObjectsRequest: one entry per object the request named.
constexpr uint64_t kMaxAssignedObjects = 4096;
}

void AssignObjectsResponse::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Objects.size());

    for (const auto& object : Objects)
    {
        object.Serialize(aWriter);
    }
}

void AssignObjectsResponse::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    const uint64_t count = Serialization::ReadVarInt(aReader);
    if (count > kMaxAssignedObjects)
        return;

    Objects.resize(count);

    for (auto i = 0u; i < count; ++i)
    {
        Objects[i].Deserialize(aReader);
    }
}
