#pragma once

#include <Services/ActorMutationAuthorityPolicy.h>
#include <TiltedCore/Stl.hpp>

#include <cmath>
#include <cstdint>

struct ActorHealthChangePolicy final
{
    static constexpr uint32_t kHealthActorValue = 24;

    static constexpr bool IsAuthorized(
        const bool aEntityExists,
        const bool aOwnerExists,
        const bool aIsCurrentOwner,
        const uint32_t aOwnershipEpoch) noexcept
    {
        return ActorMutationAuthorityPolicy::IsCurrentOwner(aEntityExists, aOwnerExists, aIsCurrentOwner, aOwnershipEpoch);
    }

    static bool TryApplySignedDelta(TiltedPhoques::Map<uint32_t, float>& aActorValues, const float aDeltaHealth) noexcept
    {
        if (!std::isfinite(aDeltaHealth))
            return false;

        auto it = aActorValues.find(kHealthActorValue);
        if (it == aActorValues.end())
            return false;

        const float newHealth = it.value() + aDeltaHealth;
        if (!std::isfinite(newHealth))
            return false;

        it.value() = newHealth;
        return true;
    }
};
