#include <catch2/catch.hpp>

#include <Game/Animation/ActionReplayCache.h>
#include <Services/FurnitureUsePolicy.h>

TEST_CASE("Seat transitions are recognized without matching seated idle variations", "[furniture]")
{
    REQUIRE(FurnitureUsePolicy::IsSeatEntryAction("IdleChairEnterToSit"));
    REQUIRE(FurnitureUsePolicy::IsSeatEntryAction("IdleSitLedgeEnter"));
    REQUIRE(FurnitureUsePolicy::IsSeatEntryAction("IdleStoolEnter"));
    REQUIRE(FurnitureUsePolicy::IsSeatEntryAction("IdleTableEnter"));
    REQUIRE_FALSE(FurnitureUsePolicy::IsSeatEntryAction("IdleChairVar1"));
    REQUIRE(FurnitureUsePolicy::IsFurnitureExitAction("IdleChairExitToStand"));
    REQUIRE(FurnitureUsePolicy::IsFurnitureExitAction("GetUpBegin"));
}

TEST_CASE("A furniture reference can be reserved by only one actor", "[furniture]")
{
    REQUIRE(FurnitureUsePolicy::CanEnter(0x1234, false));
    REQUIRE_FALSE(FurnitureUsePolicy::CanEnter(0, false));
    REQUIRE_FALSE(FurnitureUsePolicy::CanEnter(0x1234, true));
}

TEST_CASE("The player furniture animation flag is read from the synchronized graph variables", "[furniture]")
{
    AnimationVariables variables;
    variables.Booleans.resize(FurnitureUsePolicy::kIsInFurnitureBooleanIndex + 1, false);
    REQUIRE_FALSE(FurnitureUsePolicy::IsInFurniture(variables));

    variables.Booleans[FurnitureUsePolicy::kIsInFurnitureBooleanIndex] = true;
    REQUIRE(FurnitureUsePolicy::IsInFurniture(variables));
}

TEST_CASE("Late furniture entry actions use their instant replay counterpart on live updates", "[furniture][animation]")
{
    ActionEvent action;
    action.ActionId = 0x123;
    action.TargetId = 0x456;
    action.IdleId = 0x789;
    action.EventName = "IdleChairEnterToSit";
    action.TargetEventName = "IdleChairEnterToSit";

    const auto normalized = ActionReplayCache::NormalizeForImmediateReplay(action);

    REQUIRE(normalized.EventName == "IdleChairEnterInstant");
    REQUIRE(normalized.TargetEventName == "IdleChairEnterInstant");
    REQUIRE(normalized.ActionId == 0);
    REQUIRE(normalized.IdleId == 0);
    REQUIRE(normalized.TargetId == action.TargetId);
}
