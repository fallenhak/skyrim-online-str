#pragma once

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
        const bool aCasterIdProvided,
        const bool aCasterExists,
        const bool aCasterHasOwner,
        const bool aSenderOwnsCaster) noexcept
    {
        if (!aTargetExists || !aTargetHasOwner)
            return false;

        if (aCasterIdProvided && (!aCasterExists || !aCasterHasOwner))
            return false;

        return aSenderOwnsTarget || (aCasterIdProvided && aSenderOwnsCaster);
    }
};
