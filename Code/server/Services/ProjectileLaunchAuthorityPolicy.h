#pragma once

#include <Services/ActorMutationAuthorityPolicy.h>
#include <cmath>
#include <cstdint>

struct ProjectileLaunchAuthorityPolicy final
{
    static constexpr bool IsAuthorized(
        const bool aEntityExists,
        const bool aOwnerExists,
        const bool aIsCurrentOwner,
        const uint32_t aOwnershipEpoch) noexcept
    {
        return ActorMutationAuthorityPolicy::IsCurrentOwner(aEntityExists, aOwnerExists, aIsCurrentOwner, aOwnershipEpoch);
    }

    static bool HasFiniteParameters(
        const float aOriginX,
        const float aOriginY,
        const float aOriginZ,
        const float aZAngle,
        const float aXAngle,
        const float aYAngle,
        const float aPower,
        const float aScale) noexcept
    {
        return std::isfinite(aOriginX) && std::isfinite(aOriginY) && std::isfinite(aOriginZ) && std::isfinite(aZAngle) &&
               std::isfinite(aXAngle) && std::isfinite(aYAngle) && std::isfinite(aPower) && std::isfinite(aScale);
    }

    // Only spell projectiles are cast from a hand, so only they must name one of the four magic
    // casting sources. Arrows and bolts carry whatever the engine left in the field; it is
    // relayed untouched.
    static constexpr bool HasValidCastingSource(const bool aIsSpellProjectile, const int32_t aCastingSource) noexcept
    {
        return !aIsSpellProjectile || (aCastingSource >= 0 && aCastingSource < 4);
    }
};
