#pragma once

#include <cstdint>

// Who simulates an NPC follows the players: the nearest one owns it. Decided once a second
// for each owned, living NPC from the owner's and the nearest other in-range player's distance.
// - An owner that stopped sending the actor's movement is replaced at once (stalled).
// - Otherwise a clearly closer player takes over after staying closer for a while
//   (hysteresis, so two players at similar distances never bounce ownership).
// - An NPC in combat keeps its owner until the fight ends, unless the owner stalls.
struct OwnershipHandoffPolicy final
{
    // The owner sends every owned actor's movement ten times a second.
    static constexpr double kStallSeconds = 5.0;
    static constexpr double kSustainSeconds = 5.0;
    // Closer by both a factor and a margin (game units, 1024 ~ 15 m).
    static constexpr float kCloserFactor = 1.5f;
    static constexpr float kMinGap = 1024.f;

    enum class Decision : uint8_t
    {
        kKeep,
        // A closer candidate appeared or changed: start (or restart) timing it.
        kStartTiming,
        kHandOffProximity,
        kHandOffStalled,
    };

    struct Input
    {
        bool HasCandidate{};
        // Distance of the owner's character to the actor; negative when unknown (no character or out of range).
        float OwnerDistance{-1.f};
        float CandidateDistance{};
        bool IsSameCandidateAsTimed{};
        double CandidateTimedSeconds{};
        double SecondsSinceOwnerUpdate{};
        bool IsInCombat{};
    };

    static constexpr bool IsClearlyCloser(const float aOwnerDistance, const float aCandidateDistance) noexcept
    {
        if (aOwnerDistance < 0.f)
            return true;
        return aCandidateDistance * kCloserFactor < aOwnerDistance && aOwnerDistance - aCandidateDistance > kMinGap;
    }

    static constexpr Decision Decide(const Input& acInput) noexcept
    {
        if (!acInput.HasCandidate)
            return Decision::kKeep;

        if (acInput.SecondsSinceOwnerUpdate > kStallSeconds)
            return Decision::kHandOffStalled;

        if (acInput.IsInCombat || !IsClearlyCloser(acInput.OwnerDistance, acInput.CandidateDistance))
            return Decision::kKeep;

        if (!acInput.IsSameCandidateAsTimed)
            return Decision::kStartTiming;

        return acInput.CandidateTimedSeconds >= kSustainSeconds ? Decision::kHandOffProximity : Decision::kKeep;
    }
};
