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
