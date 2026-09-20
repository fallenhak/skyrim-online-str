#include <Services/ProgressionService.h>

#include <Components.h>
#include <Game/Player.h>
#include <Messages/NotifyProgressionAward.h>
#include <Services/SessionService.h>
#include <Structs/ProgressionAwardPolicy.h>
#include <World.h>

#include <spdlog/spdlog.h>

#include <cmath>
#include <limits>

ProgressionService::ProgressionService(World& aWorld, entt::dispatcher&) noexcept
    : m_world(aWorld)
{
}

bool ProgressionService::IssueSkillExperienceAward(
    Player& aPlayer,
    const ProgressionSkill aSkill,
    const float aExperience,
    const ProgressionAwardReason aReason) noexcept
{
    if (!IsValidProgressionSkill(aSkill) || !IsValidProgressionAwardReason(aReason) || !std::isfinite(aExperience) ||
        aExperience <= 0.0f || aExperience > kMaxProgressionAwardExperience)
    {
        spdlog::debug("Rejected progression award for player {:X}: invalid skill, reason, or experience.", aPlayer.GetId());
        return false;
    }

    const auto character = aPlayer.GetCharacter();
    if (!character.has_value() || !m_world.valid(*character) || !m_world.all_of<CharacterComponent, PersistentCharacterComponent>(*character))
    {
        spdlog::debug("Rejected progression award for player {:X}: no live persistent character.", aPlayer.GetId());
        return false;
    }

    const auto* pSession = m_world.GetSessionService().Get(aPlayer.GetConnectionId());
    if (!pSession || pSession->State != SessionState::kInWorld)
    {
        spdlog::debug("Rejected progression award for player {:X}: session is not in-world.", aPlayer.GetId());
        return false;
    }

    const auto& persistentCharacter = m_world.get<PersistentCharacterComponent>(*character);
    if (persistentCharacter.CharacterId <= 0)
    {
        spdlog::debug("Rejected progression award for player {:X}: persistent character id is invalid.", aPlayer.GetId());
        return false;
    }

    NotifyProgressionAward notify{};
    notify.AwardId = NextAwardId();
    notify.CharacterId = static_cast<std::uint64_t>(persistentCharacter.CharacterId);
    notify.Skill = aSkill;
    notify.Experience = aExperience;
    notify.Reason = aReason;

    aPlayer.Send(notify);
    spdlog::debug("Issued progression award {} for character {} to player {:X}.", notify.AwardId, notify.CharacterId, aPlayer.GetId());
    return true;
}

std::uint64_t ProgressionService::NextAwardId() noexcept
{
    const auto awardId = m_nextAwardId;
    if (m_nextAwardId == std::numeric_limits<std::uint64_t>::max())
        m_nextAwardId = 1;
    else
        ++m_nextAwardId;

    return awardId;
}
