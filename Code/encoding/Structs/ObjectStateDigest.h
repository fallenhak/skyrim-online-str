#pragma once

#include <Structs/GameId.h>
#include <Structs/Inventory.h>

// Desync detector (world-state plan, phase 0): a client's view of one synced
// reference, compared by the server with its own record. Report only; nothing
// is corrected from it.
struct ObjectStateDigest
{
    struct ItemCount
    {
        GameId BaseId{};
        int32_t Count{};

        bool operator==(const ItemCount& acRhs) const noexcept { return BaseId == acRhs.BaseId && Count == acRhs.Count; }
    };

    enum Flags : uint8_t
    {
        kDisabled = 1 << 0,
        kLocked = 1 << 1,
        kDoorOpen = 1 << 2,
        kHasInventory = 1 << 3,
    };

    bool operator==(const ObjectStateDigest& acRhs) const noexcept;
    bool operator!=(const ObjectStateDigest& acRhs) const noexcept { return !(*this == acRhs); }

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    // False when the item count is over the read bound; the digest is then unusable.
    bool Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool Has(Flags aFlag) const noexcept { return (StateFlags & aFlag) != 0; }

    // Merged by base id, positive counts only, sorted: two equal contents give equal lists.
    static Vector<ItemCount> Canonicalize(const Inventory& acInventory) noexcept;

    GameId Id{};
    GameId CellId{};
    uint8_t StateFlags{};
    uint8_t LockLevel{};
    Vector<ItemCount> Items{};
};
