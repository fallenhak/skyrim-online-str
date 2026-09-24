#pragma once

#include <cstdint>

struct ActorMutationAuthorityPolicy final
{
    static constexpr bool IsCurrentOwner(
        const bool aEntityExists,
        const bool aOwnerExists,
        const bool aIsCurrentOwner,
        const uint32_t aOwnershipEpoch) noexcept
    {
        return aEntityExists && aOwnerExists && aIsCurrentOwner && aOwnershipEpoch != 0;
    }
};
