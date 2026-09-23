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
};
