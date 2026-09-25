#include <Messages/AssignObjectsRequest.h>

namespace
{
// A cell sends every synced reference at once; Bleak Falls Barrow alone has 308. The count was 8 bits and wrapped (308 -> 52).
constexpr uint64_t kMaxAssignedObjects = 4096;
}

void AssignObjectsRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Objects.size());

    for (const auto& object : Objects)
    {
        object.Serialize(aWriter);
    }
}

void AssignObjectsRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    const uint64_t count = Serialization::ReadVarInt(aReader);
    Objects.clear();
    OverLimitCount = 0;
    if (count > kMaxAssignedObjects)
    {
        OverLimitCount = count;
        return;
    }

    Objects.resize(count);

    for (auto i = 0u; i < count; ++i)
    {
        Objects[i].Deserialize(aReader);
    }
}
