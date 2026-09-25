#include <Messages/NotifyPartyJoined.h>
#include <TiltedCore/Serialization.hpp>

namespace
{
// Bounds a malformed count; parties are far smaller.
constexpr uint64_t kMaxPartyMembers = 1024;
}

void NotifyPartyJoined::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, IsLeader);
    Serialization::WriteVarInt(aWriter, LeaderPlayerId);
    Serialization::WriteVarInt(aWriter, PlayerIds.size());

    for (auto player : PlayerIds)
    {
        Serialization::WriteVarInt(aWriter, player);
    }
}

void NotifyPartyJoined::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    IsLeader = Serialization::ReadBool(aReader);
    LeaderPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    const uint64_t count = Serialization::ReadVarInt(aReader);
    if (count > kMaxPartyMembers)
        return;

    PlayerIds.resize(count);

    for (auto i = 0u; i < count; ++i)
    {
        PlayerIds[i] = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    }
}
