#include <TiltedCore/Stl.hpp>

#include <Services/OwnershipHandoffPolicy.h>

#include <catch2/catch.hpp>

using Policy = OwnershipHandoffPolicy;
using Decision = OwnershipHandoffPolicy::Decision;

namespace
{
Policy::Input Closer(const bool aSameCandidate, const double aTimed)
{
    Policy::Input input{};
    input.HasCandidate = true;
    input.OwnerDistance = 6000.f;
    input.CandidateDistance = 800.f;
    input.IsSameCandidateAsTimed = aSameCandidate;
    input.CandidateTimedSeconds = aTimed;
    input.SecondsSinceOwnerUpdate = 0.1;
    return input;
}
} // namespace

TEST_CASE("Ownership stays without a candidate or a clearly closer one", "[ownership.handoff]")
{
    REQUIRE(Policy::Decide({}) == Decision::kKeep);

    auto input = Closer(true, 60.0);
    // Closer, but not by enough: players side by side never swap.
    input.OwnerDistance = 1200.f;
    input.CandidateDistance = 700.f;
    REQUIRE(Policy::Decide(input) == Decision::kKeep);
    input.OwnerDistance = 3000.f;
    input.CandidateDistance = 2500.f;
    REQUIRE(Policy::Decide(input) == Decision::kKeep);
}

TEST_CASE("A clearly closer player takes over after staying closer", "[ownership.handoff]")
{
    REQUIRE(Policy::Decide(Closer(false, 0.0)) == Decision::kStartTiming);
    REQUIRE(Policy::Decide(Closer(true, 2.0)) == Decision::kKeep);
    REQUIRE(Policy::Decide(Closer(true, Policy::kSustainSeconds)) == Decision::kHandOffProximity);

    // The owner's position is unknown (its character left the area): the candidate is closer.
    auto input = Closer(true, Policy::kSustainSeconds);
    input.OwnerDistance = -1.f;
    REQUIRE(Policy::Decide(input) == Decision::kHandOffProximity);
}

TEST_CASE("An NPC in combat keeps its owner unless the owner stalls", "[ownership.handoff]")
{
    auto input = Closer(true, 60.0);
    input.IsInCombat = true;
    REQUIRE(Policy::Decide(input) == Decision::kKeep);

    input.SecondsSinceOwnerUpdate = Policy::kStallSeconds + 0.5;
    REQUIRE(Policy::Decide(input) == Decision::kHandOffStalled);

    // A stalled owner is replaced even by a farther player.
    input.IsInCombat = false;
    input.OwnerDistance = 100.f;
    input.CandidateDistance = 5000.f;
    REQUIRE(Policy::Decide(input) == Decision::kHandOffStalled);
}
