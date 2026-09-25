#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <cstdint>

struct CorpseRetentionComponent final
{
    explicit CorpseRetentionComponent(const std::uint64_t aExpiresAtTick) noexcept
        : ExpiresAtTick(aExpiresAtTick)
    {
    }

    std::uint64_t ExpiresAtTick{};
    bool RemovalQueued{};
    // Set by the first accepted take. Until then the owner's inventory broadcasts still seed the
    // server copy, so items the engine adds at death (pelts, DeathItem lists) are not lost.
    bool OwnerSeedClosed{};
};
