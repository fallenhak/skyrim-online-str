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

namespace
{
bool DecodeRawCombatHitObservationIsWellFormed(
    const std::uint64_t aAttackerServerId,
    const std::uint64_t aAttackerOwnershipEpoch,
    const std::uint64_t aTargetServerId,
    const std::uint64_t aTargetLifecycleGeneration,
    const std::uint64_t aObservationId)
{
    TiltedPhoques::Buffer buffer(256);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(kCombatHitObservationRequest, sizeof(ClientOpcode) * 8);
    Serialization::WriteVarInt(writer, aAttackerServerId);
    Serialization::WriteVarInt(writer, aAttackerOwnershipEpoch);
    Serialization::WriteVarInt(writer, aTargetServerId);
    Serialization::WriteVarInt(writer, aTargetLifecycleGeneration);
    Serialization::WriteVarInt(writer, aObservationId);

    TiltedPhoques::Buffer::Reader reader(&buffer);
    ClientMessageFactory factory;
    auto message = factory.Extract(reader);
    if (!message)
        return false;

    auto parsed = TiltedPhoques::CastUnique<CombatHitObservationRequest>(std::move(message));
    return parsed && parsed->IsWellFormed();
}
} // namespace

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

TEST_CASE("combat hit observation requests reject malformed identities and oversized 32-bit fields", "[combat_authority]")
{
    const auto maxServerField = std::numeric_limits<std::uint32_t>::max();
    const auto max64BitField = std::numeric_limits<std::uint64_t>::max();
    const auto overflowingServerField = static_cast<std::uint64_t>(maxServerField) + 1;

    REQUIRE(DecodeRawCombatHitObservationIsWellFormed(
        maxServerField - 1, maxServerField, maxServerField - 2, max64BitField, max64BitField));
    REQUIRE(DecodeRawCombatHitObservationIsWellFormed(0, 2, 3, 4, 5));
    REQUIRE(DecodeRawCombatHitObservationIsWellFormed(1, 2, 0, 4, 5));

    REQUIRE_FALSE(DecodeRawCombatHitObservationIsWellFormed(overflowingServerField, 2, 3, 4, 5));
    REQUIRE_FALSE(DecodeRawCombatHitObservationIsWellFormed(1, overflowingServerField, 3, 4, 5));
    REQUIRE_FALSE(DecodeRawCombatHitObservationIsWellFormed(1, 2, overflowingServerField, 4, 5));
    REQUIRE_FALSE(DecodeRawCombatHitObservationIsWellFormed(1, 2, 1, 4, 5));
    REQUIRE_FALSE(DecodeRawCombatHitObservationIsWellFormed(maxServerField, 2, 3, 4, 5));
    REQUIRE_FALSE(DecodeRawCombatHitObservationIsWellFormed(1, 0, 3, 4, 5));
    REQUIRE_FALSE(DecodeRawCombatHitObservationIsWellFormed(1, 2, maxServerField, 4, 5));
    REQUIRE_FALSE(DecodeRawCombatHitObservationIsWellFormed(1, 2, 3, 4, 0));
}

TEST_CASE("pending combat observations remain FIFO and reject input at the fixed capacity", "[combat_authority]")
{
    PendingCombatObservationStore<2> pending;
    const ValidatedHitObservation first{1, 2, 3, 4, 5, 6, 9};
    const ValidatedHitObservation second{1, 2, 3, 4, 6, 7, 9};
    const ValidatedHitObservation third{1, 2, 3, 4, 7, 8, 9};

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

    REQUIRE_FALSE(pending.TryAppend(ValidatedHitObservation{std::numeric_limits<std::uint32_t>::max(), 1, 2, 3, 4, 5, 6}));
    REQUIRE_FALSE(pending.TryAppend(ValidatedHitObservation{1, 1, 2, 3, 0, 5, 6}));
    REQUIRE_FALSE(pending.TryAppend(ValidatedHitObservation{1, 1, 1, 3, 4, 5, 6}));
    REQUIRE_FALSE(pending.TryAppend(ValidatedHitObservation{1, 1, 2, 3, 4, 0, 6}));
    REQUIRE(pending.Size() == 0);
    REQUIRE(pending.TryAppend(ValidatedHitObservation{1, 1, 2, 3, 4, 5, 6}));
}

TEST_CASE("pending combat observations support and clean up raw EnTT server ID zero", "[combat_authority]")
{
    PendingCombatObservationStore<2> pending;
    const ValidatedHitObservation zeroAttacker{0, 1, 2, 3, 4, 5, 6};
    const ValidatedHitObservation zeroTarget{1, 1, 0, 7, 8, 9, 10};

    REQUIRE(pending.TryAppend(zeroAttacker));
    pending.RemoveActor(0);
    REQUIRE(pending.Size() == 0);

    REQUIRE(pending.TryAppend(zeroTarget));
    REQUIRE(pending.TakeForAcceptedHealthDecrease(0, 7) == zeroTarget);
}

TEST_CASE("accepted canonical health decrease correlates one matching pending target lifecycle", "[combat_authority]")
{
    PendingCombatObservationStore<4> pending;
    const ValidatedHitObservation firstTargetHit{1, 2, 10, 20, 30, 1, 8};
    const ValidatedHitObservation otherTargetHit{4, 5, 11, 21, 31, 2, 9};
    const ValidatedHitObservation secondTargetHit{6, 7, 10, 20, 32, 3, 10};
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
    const ValidatedHitObservation staleHit{1, 2, 10, 20, 30, 1, 7};
    const ValidatedHitObservation currentHit{1, 2, 10, 22, 31, 2, 7};
    const ValidatedHitObservation unrelatedHit{3, 4, 11, 21, 32, 3, 8};
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
    const ValidatedHitObservation discarded{1, 2, 9, 19, 29, 1, 7};
    const ValidatedHitObservation firstRetained{3, 4, 11, 21, 31, 2, 8};
    const ValidatedHitObservation matched{5, 6, 10, 20, 32, 3, 9};
    const ValidatedHitObservation lastRetained{7, 8, 12, 22, 33, 4, 10};
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

TEST_CASE("Removing an actor clears its pending observations and retains wrapped FIFO order", "[combat_authority]")
{
    PendingCombatObservationStore<5> pending;
    const ValidatedHitObservation first{1, 2, 10, 20, 1, 1, 3};
    const ValidatedHitObservation removedAsAttacker{17, 2, 11, 21, 2, 2, 4};
    const ValidatedHitObservation retainedBeforeRemovedTarget{3, 2, 12, 22, 3, 3, 5};
    const ValidatedHitObservation removedAsTarget{4, 2, 17, 23, 4, 4, 6};
    const ValidatedHitObservation retainedAfterRemovedTarget{5, 2, 13, 24, 5, 5, 7};

    REQUIRE(pending.TryAppend(first));
    REQUIRE(pending.TryAppend(removedAsAttacker));
    REQUIRE(pending.TryAppend(retainedBeforeRemovedTarget));
    REQUIRE(pending.TryAppend(removedAsTarget));
    REQUIRE(pending.Pop() == first); // move the ring head before the next append
    REQUIRE(pending.TryAppend(retainedAfterRemovedTarget));

    pending.RemoveActor(17);

    REQUIRE(pending.Size() == 2);
    REQUIRE(pending.Pop() == retainedBeforeRemovedTarget);
    REQUIRE(pending.Pop() == retainedAfterRemovedTarget);
    REQUIRE_FALSE(pending.Pop().has_value());
}
