#include <TiltedCore/Stl.hpp>
#include <optional>

#include <Services/CharacterInventoryPolicy.h>

#include <catch2/catch.hpp>

TEST_CASE("Leveled NPC reconciliation uses the pending spawn inventory before 3D is ready", "[inventory]")
{
    Inventory currentInventory{};
    Inventory pendingSpawnInventory{};

    Inventory::Entry weapon{};
    weapon.BaseId = GameId{1, 0x1234};
    weapon.Count = 1;
    weapon.ExtraWorn = true;
    pendingSpawnInventory.Entries.push_back(weapon);
    pendingSpawnInventory.CurrentMagicEquipment.RightHandSpell = GameId{1, 0x5678};

    const Inventory& selected = CharacterInventoryPolicy::GetLeveledConformSnapshot(currentInventory, &pendingSpawnInventory);

    REQUIRE(selected.Entries == pendingSpawnInventory.Entries);
    REQUIRE(selected.CurrentMagicEquipment == pendingSpawnInventory.CurrentMagicEquipment);
}

TEST_CASE("Leveled NPC reconciliation uses current actor inventory when no spawn snapshot is pending", "[inventory]")
{
    Inventory currentInventory{};
    Inventory::Entry weapon{};
    weapon.BaseId = GameId{1, 0x1234};
    weapon.Count = 1;
    weapon.ExtraWorn = true;
    currentInventory.Entries.push_back(weapon);

    const Inventory& selected = CharacterInventoryPolicy::GetLeveledConformSnapshot(currentInventory, nullptr);

    REQUIRE(selected.Entries == currentInventory.Entries);
}
