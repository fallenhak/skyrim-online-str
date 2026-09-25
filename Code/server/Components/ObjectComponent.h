#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/LockData.h>
#include <Game/Player.h>
#include <Services/ObjectInteractionPolicy.h>

struct ObjectComponent
{
    ObjectComponent(Player* apLastSender)
        : pLastSender(apLastSender)
    {
    }

    Player* pLastSender;
    // ObjectService has no authoritative static-reference type, placement, or state source.
    // Client-discovered data remains untrusted until one exists.
    bool HasTrustedState{};
    LockData CurrentLockData{};
    // Set by the discovering client and fixed for the entity's lifetime. Receivers
    // re-check the local base form type before applying a harvest.
    bool IsHarvestable{};
    // Server-owned: flipped by the first authorized activation (ObjectInteractionPolicy::TryHarvest).
    bool IsHarvested{};
    bool IsHarvestItem{};
    std::uint64_t HarvestRespawnAtUnix{};
    // Stable, client-discovered world item; the server owns its taken state.
    bool IsOpenLoot{};
    bool IsLootTaken{};
    std::uint64_t LootRespawnAtUnix{};
    // Furniture identity supplied during object discovery; interactions still require a matching server record and range.
    bool IsFurniture{};
    // Set by the discovering client; receivers re-check the local base form type.
    bool IsDoor{};
    DoorState Door{};
    bool IsActivator{};
    ActivatorState Activator{};
};
