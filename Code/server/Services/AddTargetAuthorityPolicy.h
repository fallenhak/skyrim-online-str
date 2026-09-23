#pragma once

#include <cstdint>

struct AddTargetAuthorityPolicy final
{
    // The client reports effects from either side of the interaction: the
    // caster owner reports casts applied to other actors, and the target owner
    // reports effects applied to its locally simulated actor. A missing caster
    // is valid only through the target-owner path.
    [[nodiscard]] static constexpr bool IsAuthorized(
        const bool aTargetExists,
        const bool aTargetHasOwner,
        const bool aSenderOwnsTarget,
        const uint32_t aTargetOwnershipEpoch,
        const uint32_t aCurrentTargetOwnershipEpoch,
        const bool aCasterIdProvided,
        const bool aCasterExists,
        const bool aCasterHasOwner,
        const bool aSenderOwnsCaster,
        const uint32_t aCasterOwnershipEpoch,
        const uint32_t aCurrentCasterOwnershipEpoch) noexcept
    {
        if (!aTargetExists || !aTargetHasOwner || aTargetOwnershipEpoch == 0 ||
            aTargetOwnershipEpoch != aCurrentTargetOwnershipEpoch)
            return false;

        if (aCasterIdProvided && (!aCasterExists || !aCasterHasOwner || aCasterOwnershipEpoch == 0 ||
                                  aCasterOwnershipEpoch != aCurrentCasterOwnershipEpoch))
            return false;

        if (!aCasterIdProvided &&
            (aCasterExists || aCasterHasOwner || aSenderOwnsCaster || aCasterOwnershipEpoch != 0 || aCurrentCasterOwnershipEpoch != 0))
            return false;

        return aSenderOwnsTarget || (aCasterIdProvided && aSenderOwnsCaster);
    }
};
