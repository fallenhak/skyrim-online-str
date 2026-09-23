#pragma once

#include <cstdint>

/**
 * @brief Server-internal signal emitted after an accepted health update lowers
 * the canonical health value of the current actor lifecycle.
 *
 * The event deliberately carries identity only. Its listeners must not infer
 * damage magnitude from the client request that caused the accepted update.
 */
struct AcceptedCanonicalHealthDecreaseEvent final
{
    using ServerId = std::uint32_t;
    using LifecycleGeneration = std::uint64_t;

    constexpr AcceptedCanonicalHealthDecreaseEvent(const ServerId aServerId, const LifecycleGeneration aLifecycleGeneration) noexcept
        : TargetServerId(aServerId)
        , TargetLifecycleGeneration(aLifecycleGeneration)
    {
    }

    const ServerId TargetServerId;
    const LifecycleGeneration TargetLifecycleGeneration;
};
