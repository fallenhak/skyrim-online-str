#pragma once

#include <Structs/Inventory.h>

#include <cstdint>
#include <optional>

struct TESObjectREFR;

// Moves between the local player and a synced container go to the server as one
// RequestContainerTransfer instead of two independent inventory changes.
namespace ContainerTransfers
{
// Mirrors ContainerTransferTarget on the server.
enum class TargetKind : uint8_t
{
    kObject = 0,
    kCorpse = 1,
};

struct Target
{
    uint32_t ServerId{};
    TargetKind Kind{};
};

// What the server tracks behind this reference: a synced container object, or a dead NPC it
// knows by server id. nullopt for anything else (living actors, players, unsynced player-home
// chests, containers without a server entity yet).
[[nodiscard]] std::optional<Target> GetSyncedTarget(const TESObjectREFR* apReference) noexcept;

// How many units that can merge with acItem the reference currently holds.
[[nodiscard]] int32_t CountOf(const TESObjectREFR* apReference, const Inventory::Entry& acItem) noexcept;
} // namespace ContainerTransfers
