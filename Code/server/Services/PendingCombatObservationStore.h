#pragma once

#include <Services/ValidatedHitObservation.h>

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

/**
 * @brief Fixed-capacity FIFO for server-validated observations awaiting later
 * canonical health/death correlation.
 *
 * A full store rejects new observations instead of evicting pending evidence.
 * The queue retains identity and ordering metadata only; it cannot apply
 * damage, establish a kill, or award anything.
 */
template <std::size_t tCapacity = 1024>
class PendingCombatObservationStore final
{
    static_assert(tCapacity > 0, "A pending combat observation store must have capacity.");

public:
    static constexpr std::size_t kCapacity = tCapacity;

    [[nodiscard]] bool CanAppend() const noexcept
    {
        return m_count < tCapacity;
    }

    [[nodiscard]] bool TryAppend(const ValidatedHitObservation& acObservation) noexcept
    {
        if (!acObservation.IsWellFormed() || !CanAppend())
            return false;

        const auto index = (m_head + m_count) % tCapacity;
        m_observations[index].emplace(acObservation);
        ++m_count;
        return true;
    }

    [[nodiscard]] std::optional<ValidatedHitObservation> Pop() noexcept
    {
        if (m_count == 0)
            return std::nullopt;

        std::optional<ValidatedHitObservation> observation{std::move(m_observations[m_head])};
        m_observations[m_head].reset();
        m_head = (m_head + 1) % tCapacity;
        --m_count;
        return observation;
    }

    /**
     * @brief Removes at most one observation for the target lifecycle after
     * an accepted canonical health decrease.
     *
     * The oldest observation for the matching target lifecycle is returned.
     * Stale observations for the same server entity ID are discarded because
     * lifecycle generations never become current again. Other targets retain
     * their FIFO order.
     */
    [[nodiscard]] std::optional<ValidatedHitObservation> TakeForAcceptedHealthDecrease(
        const ValidatedHitObservation::ServerId aTargetServerId,
        const ValidatedHitObservation::LifecycleGeneration aTargetLifecycleGeneration) noexcept
    {
        return TakeForAcceptedHealthDecrease(
            aTargetServerId,
            aTargetLifecycleGeneration,
            [](const ValidatedHitObservation&) noexcept { return true; });
    }

    /**
     * @brief Select the oldest still-authorized observation for a target
     * lifecycle after an accepted canonical health decrease.
     *
     * Stale target incarnations and observations whose attacker no longer has
     * current authority are discarded while scanning. This lets a transfer
     * or disconnect invalidate its queued reports without allowing an old
     * report to consume a later owner's accepted health decrease.
     */
    template <typename tIsStillAuthorized>
    [[nodiscard]] std::optional<ValidatedHitObservation> TakeForAcceptedHealthDecrease(
        const ValidatedHitObservation::ServerId aTargetServerId,
        const ValidatedHitObservation::LifecycleGeneration aTargetLifecycleGeneration,
        tIsStillAuthorized&& aIsStillAuthorized) noexcept(noexcept(aIsStillAuthorized(std::declval<const ValidatedHitObservation&>())))
    {
        if (aTargetServerId == 0 || aTargetLifecycleGeneration == 0)
            return std::nullopt;

        std::size_t offset = 0;
        while (offset < m_count)
        {
            const auto index = (m_head + offset) % tCapacity;
            const auto& observation = *m_observations[index];
            if (observation.TargetServerId != aTargetServerId)
            {
                ++offset;
                continue;
            }

            if (observation.TargetLifecycleGeneration == aTargetLifecycleGeneration && aIsStillAuthorized(observation))
            {
                std::optional<ValidatedHitObservation> matched{observation};
                RemoveAt(offset);
                return matched;
            }

            // The target incarnation or attacker authority is stale, so this
            // pending record cannot be correlated to the current decrease.
            RemoveAt(offset);
        }

        return std::nullopt;
    }

    void Clear() noexcept
    {
        for (auto& observation : m_observations)
            observation.reset();
        m_head = 0;
        m_count = 0;
    }

    /** Remove every pending observation where the actor was either participant. */
    void RemoveActor(const ValidatedHitObservation::ServerId aServerId) noexcept
    {
        if (aServerId == ValidatedHitObservation::kInvalidServerId)
            return;

        std::size_t offset = 0;
        while (offset < m_count)
        {
            const auto index = (m_head + offset) % tCapacity;
            const auto& observation = *m_observations[index];
            if (observation.AttackerServerId == aServerId || observation.TargetServerId == aServerId)
                RemoveAt(offset);
            else
                ++offset;
        }
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return m_count;
    }

private:
    void RemoveAt(const std::size_t aOffset) noexcept
    {
        for (std::size_t offset = aOffset; offset + 1 < m_count; ++offset)
        {
            const auto destination = (m_head + offset) % tCapacity;
            const auto source = (m_head + offset + 1) % tCapacity;
            m_observations[destination].reset();
            if (m_observations[source])
                m_observations[destination].emplace(*m_observations[source]);
            m_observations[source].reset();
        }

        const auto last = (m_head + m_count - 1) % tCapacity;
        m_observations[last].reset();
        --m_count;
    }

    std::array<std::optional<ValidatedHitObservation>, tCapacity> m_observations{};
    std::size_t m_head{};
    std::size_t m_count{};
};
