#pragma once

#include <Events/AcceptedCanonicalCreatureDeathEvent.h>
#include <Services/RenewableEncounterRegistry.h>

#include <cstdint>
#include <type_traits>

/**
 * The only path a creature death takes into the renewable encounter registry
 * (roadmap W03). Combat publishes AcceptedCanonicalCreatureDeathEvent (C11)
 * once per trusted Creature's first alive-to-dead transition in its current
 * lifecycle; the reporting client is never the killer and never a raw claim.
 *
 * Deaths of creatures that belong to no encounter, repeated events and events
 * for an incarnation retired by a reset are harmless: the registry reports
 * them and changes nothing. The integration subscribes this to the server
 * dispatcher, e.g. `sink<AcceptedCanonicalCreatureDeathEvent>()`.
 */
static_assert(std::is_same_v<AcceptedCanonicalCreatureDeathEvent::ServerId, decltype(EncounterIncarnation::ServerId)>,
              "Combat and world must agree on the incarnation server id type");
static_assert(std::is_same_v<AcceptedCanonicalCreatureDeathEvent::LifecycleGeneration, decltype(EncounterIncarnation::LifecycleGeneration)>,
              "Combat and world must agree on the incarnation lifecycle generation type");

[[nodiscard]] constexpr EncounterIncarnation ToEncounterIncarnation(const AcceptedCanonicalCreatureDeathEvent& acEvent) noexcept
{
    return EncounterIncarnation{acEvent.TargetServerId, acEvent.TargetLifecycleGeneration};
}

[[nodiscard]] inline RenewableEncounterState::DeathResult RecordCanonicalCreatureDeath(
    RenewableEncounterRegistry& aRegistry, const AcceptedCanonicalCreatureDeathEvent& acEvent, const std::uint64_t aTick)
{
    return aRegistry.RecordVerifiedDeath(ToEncounterIncarnation(acEvent), aTick);
}
