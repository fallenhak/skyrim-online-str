#include <TiltedCore/Stl.hpp>
#include <catch2/catch.hpp>

#include <Game/Animation/ActionReplayCache.h>
#include <Services/FurnitureUsePolicy.h>
#include <Structs/GameId.h>

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
    const GameId target{0x12, 0x345678};
    REQUIRE(FurnitureUsePolicy::CanEnter(target, true, true, false));
    REQUIRE_FALSE(FurnitureUsePolicy::CanEnter(GameId{}, true, true, false));
    REQUIRE(FurnitureUsePolicy::CanEnter(target, false, false, false)); // undiscovered furniture stays usable
    REQUIRE_FALSE(FurnitureUsePolicy::CanEnter(target, true, false, false));
    REQUIRE_FALSE(FurnitureUsePolicy::CanEnter(target, true, true, true));
}

TEST_CASE("Furniture reservations are cleared when actor control changes", "[furniture]")
{
    GameId furnitureTarget{0x12, 0x345678};
    GameId rejectedTarget{0x34, 0x567890};
    bool hasEnteredFurniture = true;
    bool rejectedFurnitureSawActiveState = true;

    FurnitureUsePolicy::ClearReservation(
        furnitureTarget, rejectedTarget, hasEnteredFurniture, rejectedFurnitureSawActiveState);

    REQUIRE(furnitureTarget == GameId{});
    REQUIRE(rejectedTarget == GameId{});
    REQUIRE_FALSE(hasEnteredFurniture);
    REQUIRE_FALSE(rejectedFurnitureSawActiveState);
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
    action.TargetId = GameId{2, 0x456};
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
