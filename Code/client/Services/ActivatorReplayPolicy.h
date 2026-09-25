#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ActivatorReplayPolicy
{
enum class Kind : std::uint8_t
{
    kNeverReplay,
    kTwoStateToggle,
    kBoundedReplay,
};

inline constexpr std::uint32_t kMaxReplayCount = 3;

[[nodiscard]] inline constexpr char ToLowerAscii(const char aCharacter) noexcept
{
    return aCharacter >= 'A' && aCharacter <= 'Z' ? static_cast<char>(aCharacter - 'A' + 'a') : aCharacter;
}

[[nodiscard]] inline bool ContainsInsensitive(const std::string_view acText, const std::string_view acNeedle) noexcept
{
    if (acNeedle.empty() || acNeedle.size() > acText.size())
        return false;

    for (std::size_t offset = 0; offset <= acText.size() - acNeedle.size(); ++offset)
    {
        std::size_t index = 0;
        for (; index < acNeedle.size(); ++index)
        {
            if (ToLowerAscii(acText[offset + index]) != ToLowerAscii(acNeedle[index]))
                break;
        }
        if (index == acNeedle.size())
            return true;
    }

    return false;
}

// Editor IDs containing trap/hazard markers can trigger damage or scripts. For
// levers, pull chains and buttons, only the final binary state matters.
[[nodiscard]] inline Kind Classify(const std::string_view acEditorId) noexcept
{
    if (acEditorId.empty() || ContainsInsensitive(acEditorId, "trap") || ContainsInsensitive(acEditorId, "hazard") ||
        ContainsInsensitive(acEditorId, "pressureplate") || ContainsInsensitive(acEditorId, "tripwire") ||
        ContainsInsensitive(acEditorId, "alarm"))
        return Kind::kNeverReplay;

    if (ContainsInsensitive(acEditorId, "lever") || ContainsInsensitive(acEditorId, "pullchain") ||
        ContainsInsensitive(acEditorId, "pull_chain") || ContainsInsensitive(acEditorId, "button"))
        return Kind::kTwoStateToggle;

    return Kind::kBoundedReplay;
}

[[nodiscard]] inline constexpr std::uint32_t ReplayCount(
    const Kind aKind, const std::uint32_t aServerCount, const std::uint32_t aAppliedCount) noexcept
{
    if (aServerCount <= aAppliedCount || aKind == Kind::kNeverReplay)
        return 0;

    const std::uint32_t missing = aServerCount - aAppliedCount;
    if (aKind == Kind::kTwoStateToggle)
        return missing % 2;

    return missing < kMaxReplayCount ? missing : kMaxReplayCount;
}
} // namespace ActivatorReplayPolicy
