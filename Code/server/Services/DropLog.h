#pragma once

#include <Services/DropLogThrottle.h>

#include <chrono>

#include <spdlog/spdlog.h>

namespace DropLog
{
inline DropLogThrottle& Throttle() noexcept
{
    static DropLogThrottle s_throttle;
    return s_throttle;
}

inline int64_t NowMs() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// acReason is a stable key ("activate: object not registered"); the details are formatted only when logged.
template <typename... Args> void Info(const std::string_view acReason, spdlog::format_string_t<Args...> aFormat, Args&&... aArgs)
{
    const uint32_t count = Throttle().Admit(acReason, NowMs());
    if (count == 0)
        return;

    spdlog::info("[Drop] {} (x{}): {}", acReason, count, fmt::format(aFormat, std::forward<Args>(aArgs)...));
}
} // namespace DropLog
