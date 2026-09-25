#pragma once

#include <Structs/Inventory.h>

#include <cstdint>
#include <optional>

struct TESObjectREFR;

// Moves between the local player and a synced container go to the server as one
// RequestContainerTransfer instead of two independent inventory changes.
namespace ContainerTransfers
{
// Server id of a container the server tracks; nullopt for anything else (actors,
// unsynced player-home chests, containers without a server entity yet).
[[nodiscard]] std::optional<uint32_t> GetSyncedContainerServerId(const TESObjectREFR* apReference) noexcept;

// How many units that can merge with acItem the reference currently holds.
[[nodiscard]] int32_t CountOf(const TESObjectREFR* apReference, const Inventory::Entry& acItem) noexcept;
} // namespace ContainerTransfers
