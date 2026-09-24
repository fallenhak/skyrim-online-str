#include <Structs/Factions.h>
#include <TiltedCore/Serialization.hpp>

#include <limits>

using TiltedPhoques::Serialization;

bool Factions::operator==(const Factions& acRhs) const noexcept
{
    return NpcFactions == acRhs.NpcFactions && ExtraFactions == acRhs.ExtraFactions;
}

bool Factions::operator!=(const Factions& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Factions::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, NpcFactions.size());

    for (auto& entry : NpcFactions)
    {
        entry.Serialize(aWriter);
    }

    Serialization::WriteVarInt(aWriter, ExtraFactions.size());

    for (auto& entry : ExtraFactions)
    {
        entry.Serialize(aWriter);
    }
}

bool Factions::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NpcFactions.clear();
    ExtraFactions.clear();

    auto npcCount = Serialization::ReadVarInt(aReader);
    if (npcCount > Factions::kMaxEntriesPerList)
        return false;

    NpcFactions.resize(npcCount);
    for (auto& entry : NpcFactions)
    {
        if (!entry.Deserialize(aReader))
        {
            NpcFactions.clear();
            return false;
        }
    }

    auto extraCount = Serialization::ReadVarInt(aReader);
    if (extraCount > Factions::kMaxEntriesPerList)
    {
        NpcFactions.clear();
        return false;
    }

    ExtraFactions.resize(extraCount);
    for (auto& entry : ExtraFactions)
    {
        if (!entry.Deserialize(aReader))
        {
            NpcFactions.clear();
            ExtraFactions.clear();
            return false;
        }
    }

    return true;
}

void FactionUpdate::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);
    FactionsContent.Serialize(aWriter);
}

bool FactionUpdate::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    const auto ownershipEpoch = Serialization::ReadVarInt(aReader);
    if (ownershipEpoch > std::numeric_limits<uint32_t>::max())
        return false;

    OwnershipEpoch = static_cast<uint32_t>(ownershipEpoch);
    return FactionsContent.Deserialize(aReader);
}
