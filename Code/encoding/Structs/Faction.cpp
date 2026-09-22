#include <Structs/Faction.h>
#include <TiltedCore/Serialization.hpp>

#include <limits>

bool Faction::operator==(const Faction& acRhs) const noexcept
{
    return Id == acRhs.Id && Rank == acRhs.Rank;
}

bool Faction::operator!=(const Faction& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Faction::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
    aWriter.WriteBits(Rank, 8);
}

bool Faction::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    const auto baseId = TiltedPhoques::Serialization::ReadVarInt(aReader);
    const auto modId = TiltedPhoques::Serialization::ReadVarInt(aReader);
    if (baseId > std::numeric_limits<uint32_t>::max() || modId > std::numeric_limits<uint32_t>::max())
        return false;

    Id = GameId(static_cast<uint32_t>(modId), static_cast<uint32_t>(baseId));

    uint64_t tmp;
    aReader.ReadBits(tmp, 8);
    Rank = tmp & 0xFF;
    return true;
}
