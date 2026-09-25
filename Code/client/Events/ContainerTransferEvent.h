#pragma once

#include <Structs/Inventory.h>

/**
 * @brief Dispatched when the local player moves an item between itself and a synced container.
 *
 * Both halves of the move (container and player inventory) are applied locally by the game;
 * the server applies them in one step or rejects the move, in which case the client rolls back.
 */
struct ContainerTransferEvent
{
    uint32_t ContainerFormId{};
    uint32_t ContainerServerId{};
    uint8_t TargetKind{}; // ContainerTransfers::TargetKind
    uint8_t Direction{}; // 0 = take (container -> player), 1 = put (player -> container)
    int32_t ExpectedContainerCount{};
    Inventory::Entry Item{};
};
