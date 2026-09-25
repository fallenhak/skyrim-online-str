#include <Messages/RequestFactionsChanges.h>
#include <TiltedCore/Serialization.hpp>
#include <cassert>
#include <limits>
#include <utility>

namespace
{
// One entry per actor whose factions changed; bounds a malformed count.
constexpr uint64_t kMaxFactionChanges = 4096;
}

void RequestFactionsChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Changes.size());

    for (auto& change : Changes)
    {
        Serialization::WriteVarInt(aWriter, change.first);
        change.second.Serialize(aWriter);
    }
}

void RequestFactionsChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    const uint64_t count = Serialization::ReadVarInt(aReader);
    Changes.clear();
    OverLimitCount = 0;
    if (count > kMaxFactionChanges)
    {
        OverLimitCount = count;
        return;
    }

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
