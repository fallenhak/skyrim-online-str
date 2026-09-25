#include <Messages/ObjectStateReport.h>

namespace
{
// Same bound as AssignObjectsRequest: every synced reference of the loaded cells.
constexpr uint64_t kMaxReportedObjects = 4096;
}

void ObjectStateReport::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Objects.size());
    for (const auto& object : Objects)
        object.Serialize(aWriter);
}

void ObjectStateReport::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Objects.clear();
    IsMalformed = false;
    const uint64_t count = Serialization::ReadVarInt(aReader);
    if (count > kMaxReportedObjects)
    {
        IsMalformed = true;
        return;
    }

    Objects.resize(count);
    for (auto& object : Objects)
    {
        if (!object.Deserialize(aReader))
        {
            Objects.clear();
            IsMalformed = true;
            return;
        }
    }
}
