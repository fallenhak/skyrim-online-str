#pragma once

#include <Structs/GameId.h>
#include <Structs/LockData.h>
#include <Structs/Inventory.h>
#include <Structs/GridCellCoords.h>

struct ObjectData
{
    ObjectData() = default;
    ~ObjectData() = default;

    bool operator==(const ObjectData& acRhs) const noexcept;
    bool operator!=(const ObjectData& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t ServerId{};
    GameId Id{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GridCellCoords CurrentCoords{};
    LockData CurrentLockData{};
    Inventory CurrentInventory{};
    bool IsStateUntrusted{};
    // Flora or a placed ingredient: activation flips server-owned harvest state.
    bool IsHarvestable{};
    // Placed ingredient (item) rather than flora: respawns on the longer timer.
    bool IsHarvestItem{};
    bool IsHarvested{};
    // Stable plugin-placed pickupable item (not a temporary player drop).
    bool IsOpenLoot{};
    bool IsLootTaken{};
    bool IsFurniture{};
    // A non-load door; the open state is owned by the server once known.
    bool IsDoor{};
    bool IsDoorStateKnown{};
    bool IsDoorOpen{};
    // Lever, chain, button, puzzle pillar: each accepted activation is relayed.
    bool IsActivator{};
    uint32_t ActivationCount{};
    // A container: contents are server-owned and change only through RequestContainerTransfer.
    bool IsContainer{};
};
