#pragma once

#include <Structs/Progression.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

constexpr float kMaxProgressionAwardExperience = 100000.0f;

constexpr bool ShouldUseServerControlledProgression(const bool aConnectionAccepted) noexcept
{
    return aConnectionAccepted;
}

constexpr bool ShouldAcceptClientLevel(const bool aHasPersistentCharacter) noexcept
{
    return !aHasPersistentCharacter;
}

inline bool IsValidProgressionAward(
    const std::uint64_t aAwardId,
    const std::uint64_t aCharacterId,
    const ProgressionSkill aSkill,
    const ProgressionAwardReason aReason,
    const float aExperience,
    const bool aIsInWorld,
    const std::uint64_t aActiveCharacterId) noexcept
{
    return aIsInWorld && aAwardId != 0 && aCharacterId != 0 && aCharacterId == aActiveCharacterId &&
        IsValidProgressionSkill(aSkill) && IsValidProgressionAwardReason(aReason) && std::isfinite(aExperience) &&
        aExperience > 0.0f && aExperience <= kMaxProgressionAwardExperience;
}

template <std::size_t tCapacity = 512>
class ProgressionAwardDeduplication final
{
    static_assert(tCapacity > 0);

public:
    bool TryRemember(const std::uint64_t aAwardId) noexcept
    {
        if (aAwardId == 0)
            return false;

        for (std::size_t i = 0; i < m_count; ++i)
        {
            if (m_awardIds[i] == aAwardId)
                return false;
        }

        m_awardIds[m_nextIndex] = aAwardId;
        m_nextIndex = (m_nextIndex + 1) % tCapacity;
        if (m_count < tCapacity)
            ++m_count;

        return true;
    }

    void Clear() noexcept
    {
        m_awardIds.fill(0);
        m_count = 0;
        m_nextIndex = 0;
    }

private:
    std::array<std::uint64_t, tCapacity> m_awardIds{};
    std::size_t m_count{0};
    std::size_t m_nextIndex{0};
};
