#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

// Every packet the server drops is logged with its reason. Silent returns once made
// a playtest look like "nothing syncs" when one count had wrapped (#40, test 5).
// Grep: "[Drop]". Each reason is logged at most once per window; the line carries how
// many drops it stands for, so a flood stays one line per second.
class DropLogThrottle
{
public:
    static constexpr int64_t kWindowMs = 1000;

    // Returns how many drops this line stands for (itself plus the suppressed ones since
    // the last line), or 0 when the reason was logged within the window.
    [[nodiscard]] uint32_t Admit(const std::string_view acReason, const int64_t aNowMs) noexcept
    {
        try
        {
            return AdmitUnchecked(acReason, aNowMs);
        }
        catch (...)
        {
            // Out of memory while tracking a reason: log the line rather than lose it.
            return 1;
        }
    }

private:
    uint32_t AdmitUnchecked(const std::string_view acReason, const int64_t aNowMs)
    {
        auto& entry = m_entries[std::string(acReason)];
        ++entry.Pending;
        if (entry.LastLoggedMs != kNeverLogged && aNowMs - entry.LastLoggedMs < kWindowMs)
            return 0;

        entry.LastLoggedMs = aNowMs;
        const uint32_t count = entry.Pending;
        entry.Pending = 0;
        return count;
    }

    static constexpr int64_t kNeverLogged = INT64_MIN;

    struct Entry
    {
        int64_t LastLoggedMs{kNeverLogged};
        uint32_t Pending{0};
    };

    std::unordered_map<std::string, Entry> m_entries;
};
