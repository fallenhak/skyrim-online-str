#include <Structs/ObjectData.h>
#include <TiltedCore/Serialization.hpp>

using TiltedPhoques::Serialization;

bool ObjectData::operator==(const ObjectData& acRhs) const noexcept
{
    return ServerId == acRhs.ServerId && Id == acRhs.Id && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && CurrentCoords == acRhs.CurrentCoords && CurrentLockData == acRhs.CurrentLockData && CurrentInventory == acRhs.CurrentInventory && IsStateUntrusted == acRhs.IsStateUntrusted && IsHarvestable == acRhs.IsHarvestable && IsHarvestItem == acRhs.IsHarvestItem && IsHarvested == acRhs.IsHarvested && IsOpenLoot == acRhs.IsOpenLoot && IsLootTaken == acRhs.IsLootTaken && IsFurniture == acRhs.IsFurniture && IsDoor == acRhs.IsDoor && IsDoorStateKnown == acRhs.IsDoorStateKnown && IsDoorOpen == acRhs.IsDoorOpen && IsActivator == acRhs.IsActivator && ActivationCount == acRhs.ActivationCount && IsContainer == acRhs.IsContainer;
}

bool ObjectData::operator!=(const ObjectData& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ObjectData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Id.Serialize(aWriter);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    CurrentCoords.Serialize(aWriter);
    CurrentLockData.Serialize(aWriter);
    CurrentInventory.Serialize(aWriter);
    Serialization::WriteBool(aWriter, IsStateUntrusted);
    Serialization::WriteBool(aWriter, IsHarvestable);
    Serialization::WriteBool(aWriter, IsHarvestItem);
    Serialization::WriteBool(aWriter, IsHarvested);
    Serialization::WriteBool(aWriter, IsOpenLoot);
    Serialization::WriteBool(aWriter, IsLootTaken);
    Serialization::WriteBool(aWriter, IsFurniture);
    Serialization::WriteBool(aWriter, IsDoor);
    Serialization::WriteBool(aWriter, IsDoorStateKnown);
    Serialization::WriteBool(aWriter, IsDoorOpen);
    Serialization::WriteBool(aWriter, IsActivator);
    Serialization::WriteVarInt(aWriter, ActivationCount);
    Serialization::WriteBool(aWriter, IsContainer);
}

void ObjectData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Id.Deserialize(aReader);
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    CurrentCoords.Deserialize(aReader);
    CurrentLockData.Deserialize(aReader);
    CurrentInventory.Deserialize(aReader);
    IsStateUntrusted = Serialization::ReadBool(aReader);
    IsHarvestable = Serialization::ReadBool(aReader);
    IsHarvestItem = Serialization::ReadBool(aReader);
    IsHarvested = Serialization::ReadBool(aReader);
    IsOpenLoot = Serialization::ReadBool(aReader);
    IsLootTaken = Serialization::ReadBool(aReader);
    IsFurniture = Serialization::ReadBool(aReader);
    IsDoor = Serialization::ReadBool(aReader);
    IsDoorStateKnown = Serialization::ReadBool(aReader);
    IsDoorOpen = Serialization::ReadBool(aReader);
    IsActivator = Serialization::ReadBool(aReader);
    ActivationCount = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    IsContainer = Serialization::ReadBool(aReader);
}
