#include <Services/PresentationAuthorityPolicy.h>
#include <Services/ObjectInteractionPolicy.h>

#include <catch2/catch.hpp>

TEST_CASE("Script animations require a known nearby actor or object source", "[presentation_authority]")
{
    REQUIRE(PresentationAuthorityPolicy::CanRelayScriptAnimation(true, true, true, true, false, true));
    REQUIRE(PresentationAuthorityPolicy::CanRelayScriptAnimation(true, true, true, false, true, true));

    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayScriptAnimation(false, true, true, true, false, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayScriptAnimation(true, false, true, true, false, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayScriptAnimation(true, true, false, false, true, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayScriptAnimation(true, true, true, false, false, true));
    REQUIRE_FALSE(PresentationAuthorityPolicy::CanRelayScriptAnimation(true, true, true, true, false, false));
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
