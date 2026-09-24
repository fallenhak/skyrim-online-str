#pragma once

#include <Structs/Progression.h>

#include <entt/entt.hpp>

#include <cstdint>

struct Player;
struct World;

/**
 * @brief Issues server-authoritative progression awards to persistent players.
 *
 * This service deliberately has no reward producer yet. Future producers must
 * call IssueSkillExperienceAward after their own gameplay validation.
 */
struct ProgressionService final
{
    ProgressionService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~ProgressionService() noexcept = default;

    TP_NOCOPYMOVE(ProgressionService);

    [[nodiscard]] bool IssueSkillExperienceAward(
        Player& aPlayer,
        ProgressionSkill aSkill,
        float aExperience,
        ProgressionAwardReason aReason) noexcept;

private:
    [[nodiscard]] std::uint64_t NextAwardId() noexcept;

    World& m_world;
    std::uint64_t m_nextAwardId{1};
};
