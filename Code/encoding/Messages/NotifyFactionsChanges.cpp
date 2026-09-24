#include <Messages/NotifyFactionsChanges.h>
#include <TiltedCore/Serialization.hpp>
#include <cassert>
#include <limits>
#include <utility>

static const uint64_t kMaxChangeCount = 0x400;

void NotifyFactionsChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    assert(Changes.size() < 0x400);

    Serialization::WriteVarInt(aWriter, Changes.size());

    for (auto& change : Changes)
    {
        Serialization::WriteVarInt(aWriter, change.first);
        change.second.Serialize(aWriter);
    }
}

void NotifyFactionsChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Changes.clear();
    uint64_t count = Serialization::ReadVarInt(aReader);

    // Early abort as we don't want to allocate a ton of memory
    if (count >= kMaxChangeCount)
        return;

    for (auto i = 0u; i < count; ++i)
    {
        const auto serverId = Serialization::ReadVarInt(aReader);
        if (serverId > std::numeric_limits<uint32_t>::max())
        {
            Changes.clear();
            return;
        }

        FactionUpdate update;
        if (!update.Deserialize(aReader))
        {
            Changes.clear();
            return;
        }

        if (!Changes.emplace(static_cast<uint32_t>(serverId), std::move(update)).second)
        {
            Changes.clear();
            return;
        }
    }
}
