#include <Structs/MovementAuthorityPolicy.h>

#include <catch2/catch.hpp>

#include <limits>

TEST_CASE("Movement updates require the current non-zero ownership epoch", "[actor_authority]")
{
    REQUIRE(MovementAuthorityPolicy::IsAuthorized(true, true, true, 1));
    REQUIRE_FALSE(MovementAuthorityPolicy::IsAuthorized(false, true, true, 1));
    REQUIRE_FALSE(MovementAuthorityPolicy::IsAuthorized(true, false, true, 1));
    REQUIRE_FALSE(MovementAuthorityPolicy::IsAuthorized(true, true, false, 1));
    REQUIRE_FALSE(MovementAuthorityPolicy::IsAuthorized(true, true, true, 0));
}

TEST_CASE("Movement payload rejects invalid locations and oversized collections", "[actor_authority]")
{
    ReferenceUpdate update;
    update.OwnershipEpoch = 4;
    update.UpdatedMovement.CellId = GameId{0, 0x1234};
    REQUIRE(MovementAuthorityPolicy::HasValidPayload(update));

    update.UpdatedMovement.Position.x = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(MovementAuthorityPolicy::HasValidPayload(update));

    update.UpdatedMovement.Position.x = std::numeric_limits<float>::max();
    REQUIRE_FALSE(MovementAuthorityPolicy::HasValidPayload(update));

    update.UpdatedMovement.Position.x = 0.f;
    update.UpdatedMovement.Direction = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(MovementAuthorityPolicy::HasValidPayload(update));

    update.UpdatedMovement.Direction = 0.f;
    update.UpdatedMovement.Variables.Floats.push_back(std::numeric_limits<float>::quiet_NaN());
    REQUIRE_FALSE(MovementAuthorityPolicy::HasValidPayload(update));

    update.UpdatedMovement.Variables.Floats.clear();
    update.ActionEvents.resize(MovementPayloadLimits::kMaxActionEvents + 1);
    REQUIRE_FALSE(MovementAuthorityPolicy::HasValidPayload(update));

    update.ActionEvents.clear();
    update.UpdatedMovement.Variables.Floats.resize(AnimationVariables::kMaxFloatCount + 1);
    REQUIRE_FALSE(MovementAuthorityPolicy::HasValidPayload(update));
}
