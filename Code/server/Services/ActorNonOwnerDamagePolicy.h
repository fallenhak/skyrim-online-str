#pragma once

#include <cmath>
#include <cstdint>

// Interim path for damage dealt by a player who does not own the target actor.
// Only the owner simulates the actor, so without this a non-owner's hits never
// reach the canonical health. Until the validated hit protocol exists, the
// server accepts a bounded damage report from an in-range, in-world player
// for the current incarnation and forwards it to everyone, owner included.
struct ActorNonOwnerDamagePolicy final
{
    // Upper bound for one reported hit. Vanilla sneak/power attacks stay well
    // below this; anything larger is treated as a forged report.
    static constexpr float kMaxDamagePerReport = 1000.f;

    [[nodiscard]] static bool IsAccepted(
        const bool aEntityExists,
        const bool aIsDead,
        const bool aSenderInWorld,
        const bool aSenderInRange,
        const uint32_t aCurrentOwnershipEpoch,
        const uint32_t aReportedOwnershipEpoch,
        const float aDeltaHealth) noexcept
    {
        if (!aEntityExists || aIsDead || !aSenderInWorld || !aSenderInRange)
            return false;

        if (aReportedOwnershipEpoch == 0 || aReportedOwnershipEpoch != aCurrentOwnershipEpoch)
            return false;

        return std::isfinite(aDeltaHealth) && aDeltaHealth < 0.f && aDeltaHealth >= -kMaxDamagePerReport;
    }
};
