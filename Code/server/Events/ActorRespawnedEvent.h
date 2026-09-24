#pragma once

#include <cstdint>

/**
 * @brief Server-internal notification that an authorized respawn began a new
 * actor lifecycle.
 */
struct ActorRespawnedEvent final
{
    constexpr ActorRespawnedEvent(const std::uint32_t aServerId, const std::uint64_t aLifecycleGeneration) noexcept
        : ServerId(aServerId)
        , LifecycleGeneration(aLifecycleGeneration)
    {
    }

    std::uint32_t ServerId{};
    std::uint64_t LifecycleGeneration{};
};
