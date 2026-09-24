#include <Services/CombatContributionLedger.h>
#include <Events/CreatureDeathContributionEvent.h>

#include <catch2/catch.hpp>

#include <limits>
#include <vector>

TEST_CASE("Combat contribution ledger rejects invalid identities", "[combat_authority]")
{
    CombatContributionLedger ledger;

    REQUIRE_FALSE(ledger.RecordValidatedContribution({0, 1}, 12, 1));
    REQUIRE_FALSE(ledger.RecordValidatedContribution({1, 0}, 12, 1));
    REQUIRE_FALSE(ledger.RecordValidatedContribution({1, 1}, 0, 1));
    REQUIRE_FALSE(ledger.RecordValidatedContribution({1, 1}, -4, 1));
    REQUIRE(ledger.TargetCount() == 0);
    REQUIRE(ledger.ContributionCount() == 0);
}

TEST_CASE("Combat contribution ledger coalesces deterministically and consumes once", "[combat_authority]")
{
    CombatContributionLedger ledger(100);
    const CombatContributionLedger::Target target{7, 3};

    REQUIRE(ledger.RecordValidatedContribution(target, 42, 10));
    REQUIRE(ledger.RecordValidatedContribution(target, 7, 11));
    REQUIRE(ledger.RecordValidatedContribution(target, 42, 12));
    REQUIRE(ledger.TargetCount() == 1);
    REQUIRE(ledger.ContributionCount() == 2);

    const auto contributions = ledger.ConsumeContributionsForDeath(target, 20);
    REQUIRE(contributions.size() == 2);
    REQUIRE(contributions[0].AttackerCharacterId == 7);
    REQUIRE(contributions[0].ObservationCount == 1);
    REQUIRE(contributions[1].AttackerCharacterId == 42);
    REQUIRE(contributions[1].ObservationCount == 2);
    REQUIRE(contributions[1].LastObservedTick == 12);
    REQUIRE(ledger.ConsumeContributionsForDeath(target, 20).empty());
    REQUIRE(ledger.TargetCount() == 0);
}

TEST_CASE("creature death result contains only ledger-resolved CharacterIds", "[combat_authority]")
{
    CombatContributionLedger ledger(100);
    const CombatContributionLedger::Target target{7, 3};

    REQUIRE(ledger.RecordValidatedContribution(target, 42, 10));
    REQUIRE(ledger.RecordValidatedContribution(target, 7, 11));
    REQUIRE(ledger.RecordValidatedContribution(target, 42, 12));

    CreatureDeathContributionEvent result{ledger.ConsumeCharacterIdsForDeath(target, 20)};
    const std::vector<Persistence::CharacterId> expectedCharacterIds{7, 42};
    REQUIRE(result.ContributorCharacterIds == expectedCharacterIds);
    REQUIRE(ledger.ConsumeCharacterIdsForDeath(target, 20).empty());
}

TEST_CASE("Combat contribution ledger is bounded and expires old observations", "[combat_authority]")
{
    CombatContributionLedger ledger(5, 1, 2);
    const CombatContributionLedger::Target target{9, 1};

    REQUIRE(ledger.RecordValidatedContribution(target, 1, 10));
    REQUIRE(ledger.RecordValidatedContribution(target, 2, 10));
    REQUIRE_FALSE(ledger.RecordValidatedContribution(target, 3, 10));
    REQUIRE(ledger.RecordValidatedContribution(target, 1, 11));

    REQUIRE_FALSE(ledger.RecordValidatedContribution({10, 1}, 1, 11));
    REQUIRE(ledger.ContributionCount() == 2);

    ledger.ExpireOld(17);
    REQUIRE(ledger.TargetCount() == 0);
    REQUIRE(ledger.ContributionCount() == 0);
}

TEST_CASE("Combat contribution ledger removes an attacker without connection state", "[combat_authority]")
{
    CombatContributionLedger ledger;
    REQUIRE(ledger.RecordValidatedContribution({1, 1}, 12, 1));
    REQUIRE(ledger.RecordValidatedContribution({2, 1}, 12, 2));
    REQUIRE(ledger.RecordValidatedContribution({2, 1}, 13, 2));

    ledger.RemoveCharacter(12);

    REQUIRE(ledger.TargetCount() == 1);
    REQUIRE(ledger.ContributionCount() == 1);
    const auto remaining = ledger.ConsumeContributionsForDeath({2, 1}, 2);
    REQUIRE(remaining.size() == 1);
    REQUIRE(remaining.front().AttackerCharacterId == 13);
}

TEST_CASE("Combat contribution ledger isolates and clears individual targets", "[combat_authority]")
{
    CombatContributionLedger ledger;
    const CombatContributionLedger::Target firstTarget{1, 1};
    const CombatContributionLedger::Target secondTarget{2, 1};

    REQUIRE(ledger.RecordValidatedContribution(firstTarget, 10, 1));
    REQUIRE(ledger.RecordValidatedContribution(secondTarget, 20, 1));

    ledger.ClearTarget(firstTarget);

    REQUIRE(ledger.TargetCount() == 1);
    REQUIRE(ledger.ConsumeContributionsForDeath(firstTarget, 1).empty());
    const auto remaining = ledger.ConsumeContributionsForDeath(secondTarget, 1);
    REQUIRE(remaining.size() == 1);
    REQUIRE(remaining.front().AttackerCharacterId == 20);
}

TEST_CASE("Combat contribution ledger saturates observation counters", "[combat_authority]")
{
    CombatContributionLedger ledger;
    const CombatContributionLedger::Target target{4, 1};
    REQUIRE(ledger.RecordValidatedContribution(target, 2, 1));

    const auto maxCount = std::numeric_limits<std::uint32_t>::max();
    for (std::uint64_t tick = 2; tick <= 4; ++tick)
        REQUIRE(ledger.RecordValidatedContribution(target, 2, tick));

    const auto contribution = ledger.ConsumeContributionsForDeath(target, 4);
    REQUIRE(contribution.size() == 1);
    REQUIRE(contribution.front().ObservationCount == 4);
    REQUIRE(contribution.front().ObservationCount <= maxCount);
}
