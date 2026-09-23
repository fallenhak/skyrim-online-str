#include <gtest/gtest.h>

#include <Components.h>
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
    CellIdComponent TargetCell{GameId{1, 0x102}};

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

TEST(CombatTargetAuthorizationPolicy, AcceptsCurrentTrustedCreatureInSameInteriorCell)
{
    CombatTargetAuthorizationFixture fixture;

    EXPECT_TRUE(CombatTargetAuthorizationPolicy::IsAuthorized(fixture.MakeInput()));
}

TEST(CombatTargetAuthorizationPolicy, RejectsMissingEntityAndInvalidTargetIds)
{
    CombatTargetAuthorizationFixture fixture;
    auto input = fixture.MakeInput();

    input.TargetEntityExists = false;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    input.TargetServerId = 0;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST(CombatTargetAuthorizationPolicy, RejectsMissingStaleOrInvalidLifecycle)
{
    CombatTargetAuthorizationFixture fixture;
    auto input = fixture.MakeInput();

    input.ObservedTargetLifecycleGeneration++;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    input.ObservedTargetLifecycleGeneration = ActorLifecycleComponent::kInvalidGeneration;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    input.pCurrentTargetLifecycle = nullptr;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    ActorLifecycleComponent invalidLifecycle{ActorLifecycleComponent::kInvalidGeneration};
    input.pCurrentTargetLifecycle = &invalidLifecycle;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST(CombatTargetAuthorizationPolicy, RejectsUntrustedCreatureClaims)
{
    CombatTargetAuthorizationFixture fixture;
    auto input = fixture.MakeInput();
    input.pTargetPopulationIdentity = nullptr;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    auto claimedCreature = MakeIdentity(ActorPopulationIdentitySource::kClientClaimedTemporaryBase, ActorPopulationClass::kCreature);
    input.pTargetPopulationIdentity = &claimedCreature;

    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST(CombatTargetAuthorizationPolicy, RejectsPlayerHumanoidAndUnknownPopulationClasses)
{
    CombatTargetAuthorizationFixture fixture;
    auto input = fixture.MakeInput();

    auto player = MakeIdentity(ActorPopulationIdentitySource::kPlayer, ActorPopulationClass::kPlayer);
    input.pTargetPopulationIdentity = &player;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    auto playerMisclassifiedAsCreature = MakeIdentity(ActorPopulationIdentitySource::kPlayer, ActorPopulationClass::kCreature);
    input = fixture.MakeInput();
    input.pTargetPopulationIdentity = &playerMisclassifiedAsCreature;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    auto humanoid = MakeIdentity(ActorPopulationIdentitySource::kServerNpcBase, ActorPopulationClass::kHumanoidNpc);
    input = fixture.MakeInput();
    input.pTargetPopulationIdentity = &humanoid;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    auto unknown = MakeIdentity(ActorPopulationIdentitySource::kServerNpcBase, ActorPopulationClass::kUnknown);
    input = fixture.MakeInput();
    input.pTargetPopulationIdentity = &unknown;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST(CombatTargetAuthorizationPolicy, RejectsMissingOrImplausibleCells)
{
    CombatTargetAuthorizationFixture fixture;
    auto input = fixture.MakeInput();

    input.pAttackerCell = nullptr;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    input.pTargetCell = nullptr;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    CellIdComponent otherInteriorCell{GameId{1, 0x103}};
    input.pTargetCell = &otherInteriorCell;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    CellIdComponent missingCell{};
    input.pAttackerCell = &missingCell;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    input = fixture.MakeInput();
    CellIdComponent exteriorCell{GameId{1, 0x104}, GameId{1, 0x200}, GridCellCoords{0, 0}};
    input.pTargetCell = &exteriorCell;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST(CombatTargetAuthorizationPolicy, UsesOrdinaryExteriorGridWindowAndRequiresMatchingWorldspace)
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

    EXPECT_TRUE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    CellIdComponent outsideLoadedWindow{GameId{1, 0x203}, GameId{1, 0x300}, GridCellCoords{13, -8}};
    input.pTargetCell = &outsideLoadedWindow;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));

    CellIdComponent otherWorldspace{GameId{1, 0x204}, GameId{1, 0x301}, GridCellCoords{10, -10}};
    input.pTargetCell = &otherWorldspace;
    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST(CombatTargetAuthorizationPolicy, RejectsInvalidExteriorCoordinates)
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

    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}

TEST(CombatTargetAuthorizationPolicy, RejectsExtremeExteriorCoordinatesWithoutNarrowSubtraction)
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

    EXPECT_FALSE(CombatTargetAuthorizationPolicy::IsAuthorized(input));
}
} // namespace
