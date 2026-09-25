#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <array>
#include <string_view>

namespace ActivatorReplayPolicy
{
enum class Kind : std::uint8_t
{
    kNeverReplay,
    kTwoStateToggle,
    kBoundedReplay,
    // A three-sided rotating puzzle pillar: only the missing turns modulo 3 matter.
    // It ignores activations while turning, so the caller spaces them out.
    kThreeFaceRotation,
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

// The game keeps no editor IDs at runtime, so Classify("") made every activator
// kNeverReplay and late joiners never saw levers or pillars. These Skyrim.esm ACTI
// base forms were classified offline from their editor IDs (see the tests).
inline constexpr std::array<std::uint32_t, 20> kTwoStateBaseFormIds{
    0x00021513, // NorLever01
    0x0004D77E, // MetalLever01
    0x00071269, // SimpleLever
    0x0008F3F9, // DweLever01
    0x0008F3FA, // DweLever02
    0x00040579, // dunVolskyggePuzzleLever01
    0x0005EBE8, // soljundLever
    0x00071CBE, // CWWhiteDBridgeInvisLever
    0x000AB238, // dunSkuldafnPuzzleLever
    0x000BBC71, // dunKnifepointRidgePullLeverTrig
    0x000BECF5, // dunRobbersGorgePollPullLever
    0x000F13AC, // defaultPuzzleLever01NoFurn
    0x000F13B4, // defaultPuzzlePullChain01NoFurn
    0x00026858, // GenPullChain01
    0x000C6386, // GenPullChain01NoName
    0x00098665, // NorPullChain01
    0x000EA53C, // DA16MiasmaPullChain
    0x0001BA5A, // DweButton01
    0x00052E9B, // ImpButton01
    0x000AB239, // dunSkluldafnPuzzlePillar01TwoStage
};

inline constexpr std::array<std::uint32_t, 5> kThreeFaceBaseFormIds{
    0x0001717B, // RuinsPuzzlePillar01
    0x0006D38A, // SkyHavenPuzzlePillar01
    0x000AA8CE, // NorDefaultPuzzlePillar01
    0x000AB23E, // dunSkluldafnPuzzlePillar01
    0x000F13AE, // NorDefaultPuzzlePillar01NoFurn
};

// Base form table first; the editor ID only helps where a plugin keeps it.
[[nodiscard]] inline Kind ClassifyBaseForm(const std::uint32_t aBaseFormId, const std::string_view acEditorId) noexcept
{
    if (std::find(kTwoStateBaseFormIds.begin(), kTwoStateBaseFormIds.end(), aBaseFormId) != kTwoStateBaseFormIds.end())
        return Kind::kTwoStateToggle;
    if (std::find(kThreeFaceBaseFormIds.begin(), kThreeFaceBaseFormIds.end(), aBaseFormId) != kThreeFaceBaseFormIds.end())
        return Kind::kThreeFaceRotation;
    return Classify(acEditorId);
}

[[nodiscard]] inline constexpr std::uint32_t ReplayCount(
    const Kind aKind, const std::uint32_t aServerCount, const std::uint32_t aAppliedCount) noexcept
{
    if (aServerCount <= aAppliedCount || aKind == Kind::kNeverReplay)
        return 0;

    const std::uint32_t missing = aServerCount - aAppliedCount;
    if (aKind == Kind::kTwoStateToggle)
        return missing % 2;
    if (aKind == Kind::kThreeFaceRotation)
        return missing % 3;

    return missing < kMaxReplayCount ? missing : kMaxReplayCount;
}
} // namespace ActivatorReplayPolicy
