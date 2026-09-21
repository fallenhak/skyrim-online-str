#define TP_INTERNAL_COMPONENTS_GUARD
#include <Components/ActorLifecycleComponent.h>
#undef TP_INTERNAL_COMPONENTS_GUARD

#include <catch2/catch.hpp>

#include <optional>

TEST_CASE("Actor lifecycle generations are server-owned and monotonic", "[actor_lifecycle]")
{
    const ActorLifecycleComponent first;
    const ActorLifecycleComponent second;

    REQUIRE(first.IsValid());
    REQUIRE(second.IsValid());
    REQUIRE(second.GetGeneration() > first.GetGeneration());
    REQUIRE(second.LifecycleGeneration > first.LifecycleGeneration);
}

TEST_CASE("Actor lifecycle cleanup does not make a generation reusable", "[actor_lifecycle]")
{
    std::optional<ActorLifecycleComponent> lifecycle;
    lifecycle.emplace();
    const auto removedGeneration = lifecycle->GetGeneration();
    lifecycle.reset();

    lifecycle.emplace();
    REQUIRE(lifecycle->IsValid());
    REQUIRE(lifecycle->GetGeneration() > removedGeneration);
}

TEST_CASE("Zero is not a valid actor lifecycle generation", "[actor_lifecycle]")
{
    const ActorLifecycleComponent invalid{ActorLifecycleComponent::kInvalidGeneration};
    REQUIRE_FALSE(invalid.IsValid());
}
