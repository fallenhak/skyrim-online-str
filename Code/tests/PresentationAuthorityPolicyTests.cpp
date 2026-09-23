#include <Services/PresentationAuthorityPolicy.h>
#include <Services/ObjectInteractionPolicy.h>

#include <catch2/catch.hpp>

TEST_CASE("Script animations require a known nearby NPC source", "[presentation_authority]")
{
    REQUIRE(PresentationAuthorityPolicy::CanRelayScriptAnimation(true, true, true, true, true));

    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayScriptAnimation(false, true, true, true, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayScriptAnimation(true, false, true, true, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayScriptAnimation(true, true, false, true, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayScriptAnimation(true, true, true, false, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayScriptAnimation(true, true, true, true, false));
}

TEST_CASE("A provisionally registered object cannot relay a script animation", "[presentation_authority]")
{
    const GameId senderCell{0, 1};
    const GameId objectCell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GameId clientChosenFormId{1, 0x200};
    const GridCellCoords senderCoords{10, -10};
    const GridCellCoords objectCoords{12, -8};

    // AssignObjectsRequest permits a nearby client-discovered reference and
    // registers it with form/cell data, but does not give it a CharacterComponent.
    REQUIRE(ObjectInteractionPolicy::CanDiscover(
        clientChosenFormId, senderCell, worldSpace, senderCoords,
        objectCell, worldSpace, objectCoords));

    const bool registeredEntity = true;
    const bool hasFormId = true;
    const bool hasCell = true;
    // OnScriptAnimationRequest classifies the registered ObjectComponent as
    // non-NPC; the client-reported location remains provisional.
    const bool isNpcCharacter = false;
    const bool senderInRange = true;

    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayScriptAnimation(
        registeredEntity, hasFormId, hasCell, isNpcCharacter, senderInRange));
}

TEST_CASE("Dialogue and subtitles require a nearby registered NPC source", "[presentation_authority]")
{
    // Non-owner interaction remains valid when the target is a known nearby NPC.
    REQUIRE(PresentationAuthorityPolicy::CanRelayNpcPresentation(true, true, true, true, true));

    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayNpcPresentation(false, true, true, true, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayNpcPresentation(true, false, true, true, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayNpcPresentation(true, true, false, true, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayNpcPresentation(true, true, true, false, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayNpcPresentation(true, true, true, true, false));
}

TEST_CASE("Presentation source range matches the loaded cell radius", "[presentation_authority]")
{
    const GameId senderCell{0, 1};
    const GameId sourceCell{0, 2};
    const GameId worldSpace{0, 0x3C};
    const GridCellCoords senderCoords{10, -10};

    REQUIRE(ObjectInteractionPolicy::IsInSenderRange(
        senderCell, worldSpace, senderCoords, sourceCell, worldSpace, GridCellCoords{12, -8}));
    REQUIRE_FALSE(ObjectInteractionPolicy::IsInSenderRange(
        senderCell, worldSpace, senderCoords, sourceCell, worldSpace, GridCellCoords{13, -10}));
    REQUIRE(ObjectInteractionPolicy::IsInSenderRange(
        senderCell, worldSpace, senderCoords, sourceCell, worldSpace, GridCellCoords{20, 0}, true));
    REQUIRE_FALSE(ObjectInteractionPolicy::IsInSenderRange(
        senderCell, worldSpace, senderCoords, sourceCell, worldSpace, GridCellCoords{21, 0}, true));
}
