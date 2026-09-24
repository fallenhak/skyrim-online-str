#include <Services/CombatAttackerAuthorizationPolicy.h>

#include <catch2/catch.hpp>

#include <limits>

namespace
{
CombatAttackerAuthorizationInput MakeValidCombatAttackerAuthorizationInput()
{
    CombatAttackerAuthorizationInput input{};
    input.SessionIsInWorld = true;
    input.SessionCharacterId = 42;
    input.AttackerServerId = 17;
    input.AttackerEntityExists = true;
    input.AttackerIsPlayerCharacter = true;
    input.OwnerExists = true;
    input.SenderIsCurrentOwner = true;
    input.RequestedOwnershipEpoch = 3;
    input.CurrentOwnershipEpoch = 3;
    input.ServerResolvedPersistentCharacterId = 42;
    return input;
}
} // namespace

TEST_CASE("Combat attacker authorization returns only the session-bound persistent identity", "[combat_authority]")
{
    const auto input = MakeValidCombatAttackerAuthorizationInput();

    REQUIRE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input) == 42);
}

TEST_CASE("Combat attacker authorization requires an in-world session", "[combat_authority]")
{
    auto input = MakeValidCombatAttackerAuthorizationInput();
    input.SessionIsInWorld = false;

    REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
}

TEST_CASE("Combat attacker authorization requires a live entity and its current owner", "[combat_authority]")
{
    SECTION("missing attacker")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.AttackerEntityExists = false;
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }

    SECTION("EnTT null server entity ID")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.AttackerServerId = std::numeric_limits<std::uint32_t>::max();
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }

    SECTION("attacker is not a player character")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.AttackerIsPlayerCharacter = false;
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }

    SECTION("missing owner")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.OwnerExists = false;
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }

    SECTION("sender is not the owner")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.SenderIsCurrentOwner = false;
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }
}

TEST_CASE("Combat attacker authorization accepts raw EnTT server ID zero", "[combat_authority]")
{
    auto input = MakeValidCombatAttackerAuthorizationInput();
    input.AttackerServerId = 0;
    REQUIRE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input) == 42);
}

TEST_CASE("Combat attacker authorization requires the current nonzero ownership epoch", "[combat_authority]")
{
    SECTION("missing requested epoch")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.RequestedOwnershipEpoch = 0;
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }

    SECTION("missing current epoch")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.CurrentOwnershipEpoch = 0;
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }

    SECTION("stale requested epoch")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.RequestedOwnershipEpoch = 2;
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }
}

TEST_CASE("Combat attacker authorization requires a valid server-resolved identity bound to the session", "[combat_authority]")
{
    SECTION("session has no selected character")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.SessionCharacterId.reset();
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }

    SECTION("session identity is invalid")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.SessionCharacterId = 0;
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }

    SECTION("attacker has no persistent identity")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.ServerResolvedPersistentCharacterId.reset();
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }

    SECTION("attacker identity is invalid")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.ServerResolvedPersistentCharacterId = -1;
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }

    SECTION("attacker identity differs from the selected character")
    {
        auto input = MakeValidCombatAttackerAuthorizationInput();
        input.ServerResolvedPersistentCharacterId = 99;
        REQUIRE_FALSE(CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(input).has_value());
    }
}
