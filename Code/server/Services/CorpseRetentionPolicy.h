#pragma once

#include <cstdint>
#include <limits>

/** Pure lifetime rules for server-owned creature corpses. Ticks are real-time seconds. */
struct CorpseRetentionPolicy final
{
    static constexpr std::uint32_t kDefaultLifetimeSeconds = 30 * 60;

    [[nodiscard]] static constexpr std::uint64_t ExpirationTick(
        const std::uint64_t aDeathTick, const std::uint32_t aLifetimeSeconds) noexcept
    {
        const auto lifetime = static_cast<std::uint64_t>(aLifetimeSeconds);
        return lifetime > std::numeric_limits<std::uint64_t>::max() - aDeathTick
            ? std::numeric_limits<std::uint64_t>::max()
            : aDeathTick + lifetime;
    }

    [[nodiscard]] static constexpr bool IsRetained(
        const bool aHasCorpseMarker, const bool aIsDead, const std::uint64_t aExpiresAtTick,
        const std::uint64_t aNowTick) noexcept
    {
        return aHasCorpseMarker && aIsDead && aNowTick < aExpiresAtTick;
    }

    [[nodiscard]] static constexpr bool IsExpired(
        const bool aHasCorpseMarker, const bool aIsDead, const std::uint64_t aExpiresAtTick,
        const std::uint64_t aNowTick) noexcept
    {
        return aHasCorpseMarker && aIsDead && aNowTick >= aExpiresAtTick;
    }

    // Once the server accepts a canonical creature death, that incarnation
    // cannot be revived by another owner-state report. A respawn is a new actor
    // lifecycle, which receives no corpse marker.
    [[nodiscard]] static constexpr bool AllowsDeathStateChange(
        const bool aHasCorpseMarker, const bool aIsDead, const bool aRequestedDead) noexcept
    {
        return !aHasCorpseMarker || !aIsDead || aRequestedDead;
    }
};
