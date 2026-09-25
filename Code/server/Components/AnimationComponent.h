#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Game/Animation/ActionReplayCache.h>
#include <Structs/ActionEvent.h>

struct AnimationComponent
{
    Vector<ActionEvent> Actions;
    ActionEvent CurrentAction;
    ActionReplayCache ActionsReplayCache;
    uint32_t FurnitureUseTargetId{};
    uint32_t RejectedFurnitureTargetId{};
    bool HasEnteredFurniture{};
    bool RejectedFurnitureSawActiveState{};
};
