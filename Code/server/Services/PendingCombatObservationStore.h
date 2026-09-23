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

    void Clear() noexcept
    {
        for (auto& observation : m_observations)
            observation.reset();
        m_head = 0;
        m_count = 0;
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return m_count;
    }

private:
    std::array<std::optional<ValidatedHitObservation>, tCapacity> m_observations{};
    std::size_t m_head{};
    std::size_t m_count{};
};
