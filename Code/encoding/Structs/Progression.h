#pragma once

#include <cstdint>
#include <type_traits>

// These values are a protocol-facing identity for Skyrim skills.  Keep them
// explicit and independent from the game's ActorValueInfo numeric values.
enum class ProgressionSkill : std::uint8_t
{
    kOneHanded = 0,
    kTwoHanded = 1,
    kArchery = 2,
    kBlock = 3,
    kSmithing = 4,
    kHeavyArmor = 5,
    kLightArmor = 6,
    kPickpocket = 7,
    kLockpicking = 8,
    kSneak = 9,
    kAlchemy = 10,
    kSpeech = 11,
    kAlteration = 12,
    kConjuration = 13,
    kDestruction = 14,
    kIllusion = 15,
    kRestoration = 16,
    kEnchanting = 17,
    kCount = 18
};

enum class ProgressionAwardReason : std::uint8_t
{
    kCreatureKill = 0,
    kQuest = 1,
    kScripted = 2,
    kCount = 3
};

constexpr bool IsValidProgressionSkill(const ProgressionSkill aSkill) noexcept
{
    return static_cast<std::underlying_type_t<ProgressionSkill>>(aSkill) <
        static_cast<std::underlying_type_t<ProgressionSkill>>(ProgressionSkill::kCount);
}

constexpr bool IsValidProgressionAwardReason(const ProgressionAwardReason aReason) noexcept
{
    return static_cast<std::underlying_type_t<ProgressionAwardReason>>(aReason) <
        static_cast<std::underlying_type_t<ProgressionAwardReason>>(ProgressionAwardReason::kCount);
}
