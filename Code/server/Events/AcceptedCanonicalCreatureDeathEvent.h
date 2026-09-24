#pragma once

#include <cstdint>

/**
 * @brief Server-internal signal for one accepted alive-to-dead Creature transition.
 *
 * The event identifies only the canonical target incarnation. The client that
 * reported the state is its simulation owner and is not represented as a
 * killer or contributor.
 */
struct AcceptedCanonicalCreatureDeathEvent final
{
    using ServerId = std::uint32_t;
    using LifecycleGeneration = std::uint64_t;

    constexpr AcceptedCanonicalCreatureDeathEvent(const ServerId aServerId, const LifecycleGeneration aLifecycleGeneration) noexcept
        : TargetServerId(aServerId)
        , TargetLifecycleGeneration(aLifecycleGeneration)
    {
    }

    // EnTT dispatcher events need assignable value members.
    ServerId TargetServerId;
    LifecycleGeneration TargetLifecycleGeneration;
};
