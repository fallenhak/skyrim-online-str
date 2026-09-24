#pragma once

#include <Services/ValidatedHitObservation.h>

#include <array>
#include <cstddef>

/**
 * @brief Fixed-capacity replay window for validated hit observations.
 *
 * The replay key combines the attacker's server entity, ownership epoch, and
 * server-owned lifecycle generation with the target's server entity and
 * lifecycle generation. Observation ticks are deliberately excluded: they
 * are ordering metadata. Call TryRemember only after attacker and target
 * authorization, so unauthorised claims cannot evict accepted observations
 * from the window.
 *
 * This is a bounded FIFO window. Once full, the oldest key is evicted to make
 * room for a new one; callers must still validate the current target lifecycle
 * before consulting this cache.
 */
template <std::size_t tCapacity = 1024>
class CombatObservationReplayCache final
{
    static_assert(tCapacity > 0, "A combat observation replay cache must have capacity.");

    struct Key final
    {
        ValidatedHitObservation::ServerId AttackerServerId{};
        ValidatedHitObservation::OwnershipEpoch AttackerOwnershipEpoch{};
        ValidatedHitObservation::LifecycleGeneration AttackerLifecycleGeneration{};
        ValidatedHitObservation::ServerId TargetServerId{};
        ValidatedHitObservation::LifecycleGeneration TargetLifecycleGeneration{};
        ValidatedHitObservation::ObservationSequence ObservationId{};

        friend constexpr bool operator==(const Key&, const Key&) noexcept = default;
    };

public:
    static constexpr std::size_t kCapacity = tCapacity;

    /**
     * @brief Remember a new observation key, returning false for malformed or
     * already-seen observations.
     *
     * A successful insert may evict the oldest key when the fixed window is
     * full. Replaying a key after eviction is outside this bounded cache's
     * retention window.
     */
    [[nodiscard]] bool TryRemember(const ValidatedHitObservation& acObservation) noexcept
    {
        if (!acObservation.IsWellFormed())
            return false;

        const Key key{
            acObservation.AttackerServerId,
            acObservation.AttackerOwnershipEpoch,
            acObservation.AttackerLifecycleGeneration,
            acObservation.TargetServerId,
            acObservation.TargetLifecycleGeneration,
            acObservation.ObservationId};

        for (std::size_t i = 0; i < m_count; ++i)
        {
            if (m_keys[i] == key)
                return false;
        }

        m_keys[m_nextIndex] = key;
        m_nextIndex = (m_nextIndex + 1) % tCapacity;
        if (m_count < tCapacity)
            ++m_count;

        return true;
    }

    void Clear() noexcept
    {
        m_keys.fill({});
        m_count = 0;
        m_nextIndex = 0;
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return m_count;
    }

private:
    std::array<Key, tCapacity> m_keys{};
    std::size_t m_count{};
    std::size_t m_nextIndex{};
};
