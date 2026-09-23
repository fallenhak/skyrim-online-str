#include <Structs/CellMovementAuthorityPolicy.h>
#include <Structs/MovementAuthorityPolicy.h>
#include <Services/TeleportAuthorityPolicy.h>

#include <catch2/catch.hpp>

#include <limits>

TEST_CASE("Grid range comparisons do not wrap for malformed coordinates", "[actor_authority]")
{
    const auto min = std::numeric_limits<int32_t>::min();
    const auto max = std::numeric_limits<int32_t>::max();

    REQUIRE_FALSE(GridCellCoords::IsCellInGridCell({min, 0}, {max - 1, 0}, false));
    REQUIRE_FALSE(GridCellCoords::IsCellInGridCell({max, 0}, {min + 1, 0}, true));
    REQUIRE_FALSE(GridCellCoords::AreGridCellsOverlapping({min, 0}, {max - 1, 0}));
    REQUIRE_FALSE(GridCellCoords::IsCellInGridCell({max, 0}, {0, 0}, false));
    REQUIRE_FALSE(GridCellCoords::IsCellInGridCell({}, {0, 0}, false));
    REQUIRE(GridCellCoords::IsCellInGridCell({12, -8}, {10, -10}, false));
}

TEST_CASE("Grid coordinate conversion safely handles non-finite and extreme positions", "[actor_authority]")
{
    const auto unset = GridCellCoords{};
    REQUIRE(GridCellCoords::CalculateGridCellCoords(std::numeric_limits<float>::quiet_NaN(), 0.f) == unset);
    REQUIRE(GridCellCoords::CalculateGridCellCoords(std::numeric_limits<float>::max(), 0.f) == unset);
    REQUIRE(GridCellCoords::CalculateGridCellCoords(-std::numeric_limits<float>::max(), 0.f) == unset);
}

TEST_CASE("Cell transition and reference movement policies require usable cell context", "[actor_authority]")
{
    const GameId noId{};
    const GameId cell{0, 0x100};
    const GameId worldSpace{0, 0x200};
    const GridCellCoords coords{0, 0};

    REQUIRE(CellMovementAuthorityPolicy::HasValidInteriorCell(cell));
    REQUIRE_FALSE(CellMovementAuthorityPolicy::HasValidInteriorCell(noId));
    REQUIRE(CellMovementAuthorityPolicy::HasValidExteriorCell(worldSpace, cell, coords));
    REQUIRE_FALSE(CellMovementAuthorityPolicy::HasValidExteriorCell(noId, cell, coords));
    REQUIRE_FALSE(CellMovementAuthorityPolicy::HasValidExteriorCell(worldSpace, noId, coords));
    REQUIRE_FALSE(CellMovementAuthorityPolicy::HasValidExteriorCell(worldSpace, cell, GridCellCoords{}));
    REQUIRE_FALSE(CellMovementAuthorityPolicy::HasValidExteriorCell(worldSpace, cell, GridCellCoords{std::numeric_limits<int32_t>::max(), 0}));
    Vector3_NetQuantize position{};
    position.x = 0.f;
    position.y = 0.f;
    position.z = 0.f;
    REQUIRE(CellMovementAuthorityPolicy::HasValidReportedLocation(GameId{}, cell, position));
    auto malformedPosition = position;
    malformedPosition.x = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(CellMovementAuthorityPolicy::HasValidReportedLocation(GameId{}, cell, malformedPosition));
    malformedPosition = position;
    malformedPosition.x = std::numeric_limits<float>::max();
    REQUIRE_FALSE(CellMovementAuthorityPolicy::HasValidReportedLocation(GameId{}, cell, malformedPosition));
    REQUIRE(CellMovementAuthorityPolicy::HasValidReferenceLocation(worldSpace, GameId{}));
    REQUIRE(CellMovementAuthorityPolicy::HasValidReportedLocation(worldSpace, GameId{}, position));
    REQUIRE_FALSE(CellMovementAuthorityPolicy::HasValidReferenceLocation(GameId{}, GameId{}));

    ReferenceUpdate update{};
    update.OwnershipEpoch = 1;
    REQUIRE_FALSE(MovementAuthorityPolicy::HasValidPayload(update));
    update.UpdatedMovement.CellId = cell;
    REQUIRE(MovementAuthorityPolicy::HasValidPayload(update));
    update.UpdatedMovement.CellId = noId;
    update.UpdatedMovement.WorldSpaceId = worldSpace;
    REQUIRE(MovementAuthorityPolicy::HasValidPayload(update));
}

TEST_CASE("Party teleport requires both current party memberships and a valid destination", "[actor_authority]")
{
    REQUIRE(TeleportAuthorityPolicy::CanRequestPartyTeleport(true, 0, true, 0));
    REQUIRE_FALSE(TeleportAuthorityPolicy::CanRequestPartyTeleport(false, 0, true, 0));
    REQUIRE_FALSE(TeleportAuthorityPolicy::CanRequestPartyTeleport(true, 1, true, 2));

    const GameId cell{0, 0x100};
    const GameId worldSpace{0, 0x200};
    const glm::vec3 position{0.f};
    REQUIRE(TeleportAuthorityPolicy::HasValidDestination(true, true, GameId{}, cell, position));
    REQUIRE(TeleportAuthorityPolicy::HasValidDestination(true, true, worldSpace, GameId{}, position));
    REQUIRE_FALSE(TeleportAuthorityPolicy::HasValidDestination(false, true, GameId{}, cell, position));
    REQUIRE_FALSE(TeleportAuthorityPolicy::HasValidDestination(true, false, GameId{}, cell, position));
    REQUIRE_FALSE(TeleportAuthorityPolicy::HasValidDestination(true, true, GameId{}, GameId{}, position));
    REQUIRE_FALSE(TeleportAuthorityPolicy::HasValidDestination(
        true, true, GameId{}, cell, glm::vec3{std::numeric_limits<float>::infinity(), 0.f, 0.f}));
    REQUIRE_FALSE(TeleportAuthorityPolicy::HasValidDestination(
        true, true, GameId{}, cell, glm::vec3{std::numeric_limits<float>::max(), 0.f, 0.f}));
}
