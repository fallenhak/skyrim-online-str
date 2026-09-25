#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

// Rotating puzzle pillars ignore activations while they turn, and the server
// accepts at most one activation per second. Fast clicking therefore rotated a
// pillar on one client but not the other (6th test, Bleak Falls Barrow).
// A local activation is swallowed until the last one (local or remote) has had
// time to finish, so every accepted click rotates the pillar on every client.
namespace PuzzlePillarPolicy
{
// Skyrim.esm ACTI records whose editor id contains "PuzzlePillar".
inline constexpr std::array<uint32_t, 7> kBaseFormIds{
    0x0001717B, // RuinsPuzzlePillar01
    0x0006D38A, // SkyHavenPuzzlePillar01
    0x000AA8CE, // NorDefaultPuzzlePillar01 (defaultPuzzlePillarScript)
    0x000AB239, // dunSkluldafnPuzzlePillar01TwoStage
    0x000AB23E, // dunSkluldafnPuzzlePillar01
    0x000F13AE, // NorDefaultPuzzlePillar01NoFurn
    0x000F13B5, // NorDefaultPuzzlePillarPullBar01NoFurn
};

inline constexpr uint64_t kLockoutMs = 2000;

// Set while the client replays server activation history (late join): those
// activations run as the local player but must all apply.
inline bool g_isReplayingServerState = false;

[[nodiscard]] inline bool IsPuzzlePillar(const uint32_t aBaseFormId) noexcept
{
    return std::find(kBaseFormIds.begin(), kBaseFormIds.end(), aBaseFormId) != kBaseFormIds.end();
}

class Lockout
{
public:
    // Returns false when a local activation must be swallowed. Accepted local
    // activations and every remote one restart the lockout.
    [[nodiscard]] bool TryActivate(const uint32_t aRefFormId, const bool aIsLocal, const uint64_t aNowMs) noexcept
    {
        const auto it = m_lastActivationMs.find(aRefFormId);
        if (aIsLocal && it != m_lastActivationMs.end() && aNowMs - it->second < kLockoutMs)
            return false;

        m_lastActivationMs[aRefFormId] = aNowMs;
        return true;
    }

private:
    std::unordered_map<uint32_t, uint64_t> m_lastActivationMs;
};
} // namespace PuzzlePillarPolicy
