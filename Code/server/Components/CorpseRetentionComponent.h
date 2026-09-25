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
};
