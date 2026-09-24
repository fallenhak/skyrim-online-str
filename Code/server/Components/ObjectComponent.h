#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/LockData.h>
#include <Game/Player.h>

struct ObjectComponent
{
    ObjectComponent(Player* apLastSender)
        : pLastSender(apLastSender)
    {
    }

    Player* pLastSender;
    // ObjectService has no authoritative static-reference location or state source.
    // Client-discovered inventory and lock snapshots remain untrusted until one exists.
    bool HasTrustedState{};
    LockData CurrentLockData{};
    // Set by the discovering client and fixed for the entity's lifetime. Receivers
    // re-check the local base form type before applying a harvest.
    bool IsHarvestable{};
    // Server-owned: flipped by the first authorized activation (ObjectInteractionPolicy::TryHarvest).
    bool IsHarvested{};
    bool IsHarvestItem{};
    std::uint64_t HarvestRespawnAtTick{};
};
