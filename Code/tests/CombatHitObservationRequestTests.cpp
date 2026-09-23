#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>

#include <optional>
#include <functional>

#include <Messages/ClientMessageFactory.h>
#include <Services/PendingCombatObservationStore.h>

#include <catch2/catch.hpp>

#include <limits>
#include <utility>

TEST_CASE("combat hit observation requests round-trip only server entity authority and lifecycle identity", "[combat_authority]")
{
    CombatHitObservationRequest request{};
    request.AttackerServerId = 0x1234;
    request.AttackerOwnershipEpoch = 17;
    request.TargetServerId = 0x5678;
    request.TargetLifecycleGeneration = std::numeric_limits<std::uint64_t>::max() - 1;
    request.ObservationId = std::numeric_limits<std::uint64_t>::max();

    TiltedPhoques::Buffer buffer(256);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    request.Serialize(writer);

    TiltedPhoques::Buffer::Reader reader(&buffer);
    ClientMessageFactory factory;
    auto message = factory.Extract(reader);
    REQUIRE(message);
    auto parsed = TiltedPhoques::CastUnique<CombatHitObservationRequest>(std::move(message));
    REQUIRE(*parsed == request);
}

TEST_CASE("pending combat observations remain FIFO and reject input at the fixed capacity", "[combat_authority]")
{
    PendingCombatObservationStore<2> pending;
    const ValidatedHitObservation first{1, 2, 3, 4, 5, 6};
    const ValidatedHitObservation second{1, 2, 3, 4, 6, 7};
    const ValidatedHitObservation third{1, 2, 3, 4, 7, 8};

    REQUIRE(pending.CanAppend());
    REQUIRE(pending.TryAppend(first));
    REQUIRE(pending.TryAppend(second));
    REQUIRE_FALSE(pending.CanAppend());
    REQUIRE_FALSE(pending.TryAppend(third));
    REQUIRE(pending.Size() == 2);

    const auto poppedFirst = pending.Pop();
    REQUIRE(poppedFirst.has_value());
    REQUIRE(*poppedFirst == first);
    REQUIRE(pending.TryAppend(third));

    const auto poppedSecond = pending.Pop();
    const auto poppedThird = pending.Pop();
    REQUIRE(poppedSecond == second);
    REQUIRE(poppedThird == third);
    REQUIRE_FALSE(pending.Pop().has_value());

    pending.Clear();
    REQUIRE(pending.Size() == 0);
}

TEST_CASE("pending combat observations reject malformed identities without consuming capacity", "[combat_authority]")
{
    PendingCombatObservationStore<2> pending;

    REQUIRE_FALSE(pending.TryAppend(ValidatedHitObservation{0, 1, 2, 3, 4, 5}));
    REQUIRE_FALSE(pending.TryAppend(ValidatedHitObservation{1, 1, 2, 3, 0, 5}));
    REQUIRE(pending.Size() == 0);
    REQUIRE(pending.TryAppend(ValidatedHitObservation{1, 1, 2, 3, 4, 5}));
}

TEST_CASE("accepted canonical health decrease correlates one matching pending target lifecycle", "[combat_authority]")
{
    PendingCombatObservationStore<4> pending;
    const ValidatedHitObservation firstTargetHit{1, 2, 10, 20, 30, 1};
    const ValidatedHitObservation otherTargetHit{4, 5, 11, 21, 31, 2};
    const ValidatedHitObservation secondTargetHit{6, 7, 10, 20, 32, 3};
    REQUIRE(pending.TryAppend(firstTargetHit));
    REQUIRE(pending.TryAppend(otherTargetHit));
    REQUIRE(pending.TryAppend(secondTargetHit));

    const auto firstMatch = pending.TakeForAcceptedHealthDecrease(10, 20);
    REQUIRE(firstMatch == firstTargetHit);
    REQUIRE(pending.Size() == 2);

    // One canonical decrease correlates only one pending observation. The
    // unrelated target remains in FIFO order.
    const auto secondMatch = pending.TakeForAcceptedHealthDecrease(10, 20);
    REQUIRE(secondMatch == secondTargetHit);
    REQUIRE(pending.Pop() == otherTargetHit);
    REQUIRE_FALSE(pending.Pop().has_value());
}

TEST_CASE("health correlation discards stale target lifecycle observations without matching them", "[combat_authority]")
{
    PendingCombatObservationStore<4> pending;
    const ValidatedHitObservation staleHit{1, 2, 10, 20, 30, 1};
    const ValidatedHitObservation currentHit{1, 2, 10, 22, 31, 2};
    const ValidatedHitObservation unrelatedHit{3, 4, 11, 21, 32, 3};
    REQUIRE(pending.TryAppend(staleHit));
    REQUIRE(pending.TryAppend(unrelatedHit));
    REQUIRE(pending.TryAppend(currentHit));

    const auto match = pending.TakeForAcceptedHealthDecrease(10, 22);
    REQUIRE(match == currentHit);
    REQUIRE(pending.Size() == 1);
    REQUIRE(pending.Pop() == unrelatedHit);

    REQUIRE_FALSE(pending.TakeForAcceptedHealthDecrease(10, 22).has_value());
    REQUIRE_FALSE(pending.TakeForAcceptedHealthDecrease(0, 22).has_value());
    REQUIRE_FALSE(pending.TakeForAcceptedHealthDecrease(10, 0).has_value());
}

TEST_CASE("health correlation preserves FIFO order across wrapped pending storage", "[combat_authority]")
{
    PendingCombatObservationStore<3> pending;
    const ValidatedHitObservation discarded{1, 2, 9, 19, 29, 1};
    const ValidatedHitObservation firstRetained{3, 4, 11, 21, 31, 2};
    const ValidatedHitObservation matched{5, 6, 10, 20, 32, 3};
    const ValidatedHitObservation lastRetained{7, 8, 12, 22, 33, 4};
    REQUIRE(pending.TryAppend(discarded));
    REQUIRE(pending.Pop() == discarded);
    REQUIRE(pending.TryAppend(firstRetained));
    REQUIRE(pending.TryAppend(matched));
    REQUIRE(pending.TryAppend(lastRetained));

    REQUIRE(pending.TakeForAcceptedHealthDecrease(10, 20) == matched);
    REQUIRE(pending.Pop() == firstRetained);
    REQUIRE(pending.Pop() == lastRetained);
    REQUIRE_FALSE(pending.Pop().has_value());
}
