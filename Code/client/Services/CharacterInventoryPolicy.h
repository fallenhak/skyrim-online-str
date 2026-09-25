#pragma once

#include <Structs/Inventory.h>

namespace CharacterInventoryPolicy
{
inline const Inventory& GetLeveledConformSnapshot(const Inventory& acCurrentInventory, const Inventory* apPendingSpawnInventory) noexcept
{
    return apPendingSpawnInventory ? *apPendingSpawnInventory : acCurrentInventory;
}

inline void ApplyInventoryDelta(Inventory& aInventory, const Inventory::Entry& acDelta) noexcept
{
    aInventory.AddOrRemoveEntry(acDelta);
}
}
