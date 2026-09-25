#include <Structs/ObjectStateDigest.h>
#include <TiltedCore/Serialization.hpp>

#include <algorithm>

using TiltedPhoques::Serialization;

namespace
{
constexpr uint64_t kMaxDigestItems = 1024;
}

bool ObjectStateDigest::operator==(const ObjectStateDigest& acRhs) const noexcept
{
    return Id == acRhs.Id && CellId == acRhs.CellId && StateFlags == acRhs.StateFlags && LockLevel == acRhs.LockLevel && Items == acRhs.Items;
}

void ObjectStateDigest::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
    CellId.Serialize(aWriter);
    aWriter.WriteBits(StateFlags, 8);
    aWriter.WriteBits(LockLevel, 8);
    if (!Has(kHasInventory))
        return;

    Serialization::WriteVarInt(aWriter, Items.size());
    for (const auto& item : Items)
    {
        item.BaseId.Serialize(aWriter);
        Serialization::WriteVarInt(aWriter, static_cast<uint32_t>(item.Count));
    }
}

bool ObjectStateDigest::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Id.Deserialize(aReader);
    CellId.Deserialize(aReader);

    uint64_t value = 0;
    aReader.ReadBits(value, 8);
    StateFlags = value & 0xFF;
    value = 0;
    aReader.ReadBits(value, 8);
    LockLevel = value & 0xFF;

    Items.clear();
    if (!Has(kHasInventory))
        return true;

    const uint64_t count = Serialization::ReadVarInt(aReader);
    if (count > kMaxDigestItems)
        return false;

    Items.resize(count);
    for (auto& item : Items)
    {
        item.BaseId.Deserialize(aReader);
        item.Count = static_cast<int32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);
    }
    return true;
}

Vector<ObjectStateDigest::ItemCount> ObjectStateDigest::Canonicalize(const Inventory& acInventory) noexcept
{
    Vector<ItemCount> items;
    for (const auto& entry : acInventory.Entries)
    {
        auto it = std::find_if(items.begin(), items.end(), [&](const ItemCount& acItem) { return acItem.BaseId == entry.BaseId; });
        if (it == items.end())
            items.push_back({entry.BaseId, entry.Count});
        else
            it->Count += entry.Count;
    }

    items.erase(std::remove_if(items.begin(), items.end(), [](const ItemCount& acItem) { return acItem.Count <= 0; }), items.end());
    std::sort(items.begin(), items.end(), [](const ItemCount& a, const ItemCount& b) { return a.BaseId.LogFormat() < b.BaseId.LogFormat(); });
    return items;
}
