#include <catch2/catch.hpp>

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Stl.hpp>

#include <server/Components.h>
#include <Services/CombatTargetAuthorizationPolicy.h>

#include <limits>

namespace
{
ActorPopulationIdentityComponent MakeIdentity(const ActorPopulationIdentitySource aSource, const ActorPopulationClass aClassification)
{
    ActorPopulationIdentityComponent identity;
    identity.Source = aSource;
    identity.Classification = aClassification;
    return identity;
}

struct CombatTargetAuthorizationFixture
{
    ActorLifecycleComponent TargetLifecycle{17};
    ActorPopulationIdentityComponent TargetIdentity{MakeIdentity(ActorPopulationIdentitySource::kServerNpcBase, ActorPopulationClass::kCreature)};
    CellIdComponent AttackerCell{GameId{1, 0x101}};
    CellIdComponent TargetCell{GameId{1, 0x101}};

    [[nodiscard]] CombatTargetAuthorizationInput MakeInput() const noexcept
    {
        return {
            42,
            true,
            TargetLifecycle.GetGeneration(),
            &TargetLifecycle,
            &TargetIdentity,
            &AttackerCell,
            &TargetCell,
        };
    }
};

TEST_CASE("Combat target authorization accepts a current trusted Creature in the same interior cell", "[combat_authority]")
{
    CombatTargetAuthorizationFixture fixture;

    REQUIRE(CombatTargetAuthorizationPolicy::IsAuthorized(fixture.MakeInput()));
}

TEST_CASE("Combat target authorization rejects missing entities and the EnTT null ID", "[combat_authority]")
{
    CombatTargetAuthorizationFixture fixture;
    auto input = fixture.MakeInput();

    input.TargetEntityExists = false;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    input.TargetServerId = std::numeric_limits<std::uint32_t>::max();
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST_CASE("Combat target authorization accepts raw EnTT server ID zero", "[combat_authority]")
{
    CombatTargetAuthorizationFixture fixture;
    auto input = fixture.MakeInput();
    input.TargetServerId = 0;
    REQUIRE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST_CASE("Combat target authorization requires the current valid lifecycle", "[combat_authority]")
{
    CombatTargetAuthorizationFixture fixture;
    auto input = fixture.MakeInput();

    input.ObservedTargetLifecycleGeneration++;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    input.ObservedTargetLifecycleGeneration = ActorLifecycleComponent::kInvalidGeneration;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    input.pCurrentTargetLifecycle = nullptr;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    ActorLifecycleComponent invalidLifecycle{ActorLifecycleComponent::kInvalidGeneration};
    input.pCurrentTargetLifecycle = &invalidLifecycle;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST_CASE("Combat target authorization rejects missing or untrusted Creature identity", "[combat_authority]")
{
    CombatTargetAuthorizationFixture fixture;
    auto input = fixture.MakeInput();
    input.pTargetPopulationIdentity = nullptr;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    auto claimedCreature = MakeIdentity(ActorPopulationIdentitySource::kClientClaimedTemporaryBase, ActorPopulationClass::kCreature);
    input.pTargetPopulationIdentity = &claimedCreature;

    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST_CASE("Combat target authorization rejects players, humanoids, and unknown population classes", "[combat_authority]")
{
    CombatTargetAuthorizationFixture fixture;
    auto input = fixture.MakeInput();

    auto player = MakeIdentity(ActorPopulationIdentitySource::kPlayer, ActorPopulationClass::kPlayer);
    input.pTargetPopulationIdentity = &player;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    auto playerMisclassifiedAsCreature = MakeIdentity(ActorPopulationIdentitySource::kPlayer, ActorPopulationClass::kCreature);
    input = fixture.MakeInput();
    input.pTargetPopulationIdentity = &playerMisclassifiedAsCreature;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    auto humanoid = MakeIdentity(ActorPopulationIdentitySource::kServerNpcBase, ActorPopulationClass::kHumanoidNpc);
    input = fixture.MakeInput();
    input.pTargetPopulationIdentity = &humanoid;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    auto unknown = MakeIdentity(ActorPopulationIdentitySource::kServerNpcBase, ActorPopulationClass::kUnknown);
    input = fixture.MakeInput();
    input.pTargetPopulationIdentity = &unknown;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST_CASE("Combat target authorization requires plausible canonical cells", "[combat_authority]")
{
    CombatTargetAuthorizationFixture fixture;
    auto input = fixture.MakeInput();

    input.pAttackerCell = nullptr;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    input.pTargetCell = nullptr;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    CellIdComponent otherInteriorCell{GameId{1, 0x103}};
    input.pTargetCell = &otherInteriorCell;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    CellIdComponent missingCell{};
    input.pAttackerCell = &missingCell;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    CellIdComponent exteriorCell{GameId{1, 0x104}, GameId{1, 0x200}, GridCellCoords{0, 0}};
    input.pTargetCell = &exteriorCell;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST_CASE("Combat target authorization uses the ordinary exterior grid window and matching worldspace", "[combat_authority]")
{
    ActorLifecycleComponent lifecycle{31};
    auto identity = MakeIdentity(ActorPopulationIdentitySource::kServerPlacedReference, ActorPopulationClass::kCreature);
    CellIdComponent attackerCell{GameId{1, 0x201}, GameId{1, 0x300}, GridCellCoords{10, -10}};
    CellIdComponent targetCell{GameId{1, 0x202}, GameId{1, 0x300}, GridCellCoords{12, -8}};
    CombatTargetAuthorizationInput input{
        43,
        true,
        lifecycle.GetGeneration(),
        &lifecycle,
        &identity,
        &attackerCell,
        &targetCell,
    };

    REQUIRE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    CellIdComponent outsideLoadedWindow{GameId{1, 0x203}, GameId{1, 0x300}, GridCellCoords{13, -8}};
    input.pTargetCell = &outsideLoadedWindow;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    CellIdComponent otherWorldspace{GameId{1, 0x204}, GameId{1, 0x301}, GridCellCoords{10, -10}};
    input.pTargetCell = &otherWorldspace;
    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST_CASE("Combat target authorization rejects invalid exterior coordinates", "[combat_authority]")
{
    ActorLifecycleComponent lifecycle{32};
    auto identity = MakeIdentity(ActorPopulationIdentitySource::kServerNpcBase, ActorPopulationClass::kCreature);
    CellIdComponent attackerCell{GameId{1, 0x301}, GameId{1, 0x400}, GridCellCoords{4, 4}};
    CellIdComponent targetCell{GameId{1, 0x302}, GameId{1, 0x400}, GridCellCoords{}};
    CombatTargetAuthorizationInput input{
        44,
        true,
        lifecycle.GetGeneration(),
        &lifecycle,
        &identity,
        &attackerCell,
        &targetCell,
    };

    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST_CASE("Combat target authorization handles extreme exterior coordinates without narrow subtraction", "[combat_authority]")
{
    ActorLifecycleComponent lifecycle{33};
    auto identity = MakeIdentity(ActorPopulationIdentitySource::kServerNpcBase, ActorPopulationClass::kCreature);
    CellIdComponent attackerCell{
        GameId{1, 0x401},
        GameId{1, 0x500},
        GridCellCoords{std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::min()},
    };
    CellIdComponent targetCell{
        GameId{1, 0x402},
        GameId{1, 0x500},
        GridCellCoords{std::numeric_limits<std::int32_t>::max() - 1, std::numeric_limits<std::int32_t>::max() - 1},
    };
    CombatTargetAuthorizationInput input{
        45,
        true,
        lifecycle.GetGeneration(),
        &lifecycle,
        &identity,
        &attackerCell,
        &targetCell,
    };

    REQUIRE_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}
} // namespace
