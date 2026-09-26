#pragma once

#include <algorithm>
#include <cstdint>

// Automatic reconnection after an unexpected disconnect from the world.
// A player who drops never continues alone: the world stays frozen behind the
// loading screen and the client retries on its own, 1, 2, 4, 8 then every 10
// seconds, until it is back in the world or the attempts run out.
// Pure bookkeeping; TransportService performs the attempts.
class ReconnectPolicy final
{
public:
    static constexpr double kMaxDelaySeconds = 10.0;
    // About five minutes of retries; then the entry screen offers a manual retry.
    static constexpr std::uint32_t kMaxAttempts = 32;
    // A connection that was accepted but never reached the world is retried after this long.
    static constexpr double kHandshakeTimeoutSeconds = 60.0;

    // Returns true when a resume starts or continues.
    bool OnDisconnected(const bool aWasInWorld, const bool aCanReconnect, const double aNow) noexcept
    {
        if (!m_resuming)
        {
            if (!aWasInWorld || !aCanReconnect)
                return false;

            m_resuming = true;
            m_attempts = 0;
            m_nextAttemptAt = aNow + DelayFor(0);
            return true;
        }

        // A failed attempt: wait the backoff from now, never less.
        m_nextAttemptAt = std::max(m_nextAttemptAt, aNow + DelayFor(m_attempts));
        return true;
    }

    // True once when an attempt is due; the caller then connects.
    bool TakeAttempt(const double aNow) noexcept
    {
        if (!m_resuming || m_givenUp || aNow < m_nextAttemptAt)
            return false;

        if (m_attempts >= kMaxAttempts)
        {
            m_givenUp = true;
            return false;
        }

        ++m_attempts;
        m_nextAttemptAt = aNow + DelayFor(m_attempts);
        return true;
    }

    // The server accepted the connection; the character and world entry follow.
    void OnConnected(const double aNow) noexcept
    {
        if (m_resuming)
            m_nextAttemptAt = aNow + kHandshakeTimeoutSeconds;
    }

    void OnWorldEntered() noexcept { Reset(); }

    // A manual retry from the entry screen continues the same resume.
    void OnManualRetry(const double aNow) noexcept
    {
        if (!m_resuming)
            return;
        m_givenUp = false;
        m_attempts = 0;
        m_nextAttemptAt = aNow + kHandshakeTimeoutSeconds;
    }

    void Reset() noexcept
    {
        m_resuming = false;
        m_givenUp = false;
        m_attempts = 0;
        m_nextAttemptAt = 0.0;
    }

    [[nodiscard]] bool IsResuming() const noexcept { return m_resuming; }
    [[nodiscard]] bool HasGivenUp() const noexcept { return m_givenUp; }
    [[nodiscard]] std::uint32_t GetAttempts() const noexcept { return m_attempts; }
    [[nodiscard]] double GetNextAttemptAt() const noexcept { return m_nextAttemptAt; }

    static double DelayFor(const std::uint32_t aAttempts) noexcept
    {
        const double delay = static_cast<double>(1u << std::min<std::uint32_t>(aAttempts, 4u));
        return std::min(delay, kMaxDelaySeconds);
    }

private:
    bool m_resuming{};
    bool m_givenUp{};
    std::uint32_t m_attempts{};
    double m_nextAttemptAt{};
};
