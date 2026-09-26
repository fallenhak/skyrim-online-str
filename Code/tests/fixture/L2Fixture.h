#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

// A tiny plugin built from bytes so CI can run the server's plugin features without Bethesda
// files (L2 bot, #91). The plugin has no masters and loads at standard slot zero, so every
// form id below is also its resolved server id.
namespace L2Fixture
{
inline constexpr const char* kPluginName = "L2Fixture.esm";

// Records
inline constexpr uint32_t kEncounterZone = 0x000800; // ECZN, levels 5..20
inline constexpr uint32_t kLootList = 0x000801;      // LVLI: gold at level 1, key at level 10
inline constexpr uint32_t kChest = 0x000802;         // CONT: one kLootList roll and 10 gold
inline constexpr uint32_t kBandit = 0x000803;        // NPC_
inline constexpr uint32_t kBanditList = 0x000804;    // LVLN: kBandit at level 1
inline constexpr uint32_t kRace = 0x000805;          // RACE
inline constexpr uint32_t kDoor = 0x000806;          // DOOR
inline constexpr uint32_t kLever = 0x000807;         // ACTI
inline constexpr uint32_t kDummyItem = 0x000808;     // MISC, the base of the placed leveled item
inline constexpr uint32_t kKey = 0x000809;           // KEYM
inline constexpr uint32_t kBanditTemplate = 0x00080A; // NPC_ whose TPLT is kBanditList, as Skyrim's leveled actors are
inline constexpr uint32_t kGold = 0x00000F;          // Gold001's id in Skyrim.esm; only a number here

// The interior cell and its references
inline constexpr uint32_t kCell = 0x000810;
inline constexpr uint32_t kBanditRef = 0x000811;      // ACHR of kBanditTemplate
inline constexpr uint32_t kChestRef = 0x000812;       // REFR of kChest, locked (level 25, kKey)
inline constexpr uint32_t kDoorRef = 0x000813;        // REFR of kDoor
inline constexpr uint32_t kLeverRef = 0x000814;       // REFR of kLever
inline constexpr uint32_t kPlacedLootRef = 0x000815;  // REFR of kDummyItem with XLIB kLootList

inline constexpr uint8_t kZoneMinLevel = 5;
inline constexpr uint8_t kZoneMaxLevel = 20;
inline constexpr uint8_t kChestLockLevel = 25;
inline constexpr uint32_t kChestGold = 10;

[[nodiscard]] std::vector<uint8_t> BuildPlugin();

// Writes kPluginName and a loadorder.txt that lists only it into aDataDirectory.
[[nodiscard]] bool WriteDataDirectory(const std::filesystem::path& aDataDirectory);
} // namespace L2Fixture
