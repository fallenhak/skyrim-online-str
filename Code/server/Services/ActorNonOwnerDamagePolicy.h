#pragma once

#include <cmath>
#include <cstdint>

// Interim path for damage dealt by a player who does not own the target actor.
// Only the owner simulates the actor, so without this a non-owner's hits never
// reach the canonical health. Until the validated hit protocol exists, the
// server accepts a bounded damage report from an in-range, in-world player
// for the current incarnation of a non-player actor and forwards it to
// everyone, owner included.
struct ActorNonOwnerDamagePolicy final
{
    // Upper bound for one reported hit. Vanilla sneak/power attacks stay well
    // below this; anything larger is treated as a forged report.
    static constexpr float kMaxDamagePerReport = 1000.f;

    enum class Result : uint32_t
    {
        kAccepted,
        kNoTarget,
        kTargetDead,
        kTargetIsPlayer,
        kSenderNotInWorld,
        kSenderOutOfRange,
        kMissingEpoch,
        kStaleEpoch,
        kInvalidDelta,
    };

    [[nodiscard]] static Result Evaluate(
        const bool aEntityExists,
        const bool aIsDead,
        const bool aTargetIsPlayer,
        const bool aSenderInWorld,
        const bool aSenderInRange,
        const uint32_t aCurrentOwnershipEpoch,
        const uint32_t aReportedOwnershipEpoch,
        const float aDeltaHealth) noexcept
    {
        if (!aEntityExists)
            return Result::kNoTarget;
        if (aIsDead)
            return Result::kTargetDead;
        // Players are never valid targets here: PvP stays with the owner-simulated path.
        if (aTargetIsPlayer)
            return Result::kTargetIsPlayer;
        if (!aSenderInWorld)
            return Result::kSenderNotInWorld;
        if (!aSenderInRange)
            return Result::kSenderOutOfRange;
        if (aReportedOwnershipEpoch == 0)
            return Result::kMissingEpoch;
        if (aReportedOwnershipEpoch != aCurrentOwnershipEpoch)
            return Result::kStaleEpoch;
        if (!(std::isfinite(aDeltaHealth) && aDeltaHealth < 0.f && aDeltaHealth >= -kMaxDamagePerReport))
            return Result::kInvalidDelta;
        return Result::kAccepted;
    }

    [[nodiscard]] static bool IsAccepted(
        const bool aEntityExists,
        const bool aIsDead,
        const bool aTargetIsPlayer,
        const bool aSenderInWorld,
        const bool aSenderInRange,
        const uint32_t aCurrentOwnershipEpoch,
        const uint32_t aReportedOwnershipEpoch,
        const float aDeltaHealth) noexcept
    {
        return Evaluate(aEntityExists, aIsDead, aTargetIsPlayer, aSenderInWorld, aSenderInRange,
                   aCurrentOwnershipEpoch, aReportedOwnershipEpoch, aDeltaHealth) == Result::kAccepted;
    }

    // Also the DropLog reason, so each rejection reason is throttled on its own.
    [[nodiscard]] static constexpr const char* ToString(const Result aResult) noexcept
    {
        switch (aResult)
        {
        case Result::kAccepted: return "non-owner damage: accepted";
        case Result::kNoTarget: return "non-owner damage: target missing";
        case Result::kTargetDead: return "non-owner damage: target dead";
        case Result::kTargetIsPlayer: return "non-owner damage: target is a player";
        case Result::kSenderNotInWorld: return "non-owner damage: sender not in world";
        case Result::kSenderOutOfRange: return "non-owner damage: sender out of range";
        case Result::kMissingEpoch: return "non-owner damage: missing ownership epoch";
        case Result::kStaleEpoch: return "non-owner damage: stale ownership epoch";
        case Result::kInvalidDelta: return "non-owner damage: invalid health delta";
        }
        return "unknown";
    }
};
