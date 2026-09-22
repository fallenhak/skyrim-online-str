#include <Messages/RequestFactionsChanges.h>
#include <TiltedCore/Serialization.hpp>
#include <cassert>
#include <limits>
#include <utility>

void RequestFactionsChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    assert(Changes.size() < 0x100);

    aWriter.WriteBits(Changes.size() & 0xFF, 8);

    for (auto& change : Changes)
    {
        Serialization::WriteVarInt(aWriter, change.first);
        change.second.Serialize(aWriter);
    }
}

void RequestFactionsChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    uint64_t count = 0;
    aReader.ReadBits(count, 8);
    Changes.clear();

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
