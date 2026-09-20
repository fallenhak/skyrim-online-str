#include <Services/ProgressionService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/ProgressionAwardAppliedEvent.h>
#include <Games/Skyrim/ProgressionSkillMapping.h>
#include <Messages/NotifyProgressionAward.h>
#include <Services/CharacterSessionService.h>
#include <World.h>

#include <PlayerCharacter.h>

#include <spdlog/spdlog.h>

ProgressionService::ProgressionService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_awardConnection(aDispatcher.sink<NotifyProgressionAward>().connect<&ProgressionService::OnNotifyProgressionAward>(this))
    , m_disconnectedConnection(aDispatcher.sink<DisconnectedEvent>().connect<&ProgressionService::OnDisconnected>(this))
{
}

void ProgressionService::OnNotifyProgressionAward(const NotifyProgressionAward& acMessage) noexcept
{
    const auto& characterSession = m_world.GetCharacterSessionService();
    const auto& pendingSnapshot = characterSession.GetPendingCharacterLoadSnapshot();
    const std::uint64_t activeCharacterId = pendingSnapshot.has_value() ? pendingSnapshot->CharacterId : 0;

    if (!IsValidProgressionAward(
            acMessage.AwardId,
            acMessage.CharacterId,
            acMessage.Skill,
            acMessage.Reason,
            acMessage.Experience,
            characterSession.IsGameplayActive(),
            activeCharacterId))
    {
        spdlog::debug("Ignored invalid progression award {} for character {}.", acMessage.AwardId, acMessage.CharacterId);
        return;
    }

    const auto actorValue = ActorValueFromProgressionSkill(acMessage.Skill);
    if (!actorValue.has_value())
    {
        spdlog::debug("Ignored progression award {} for unsupported skill {}.", acMessage.AwardId, static_cast<unsigned>(acMessage.Skill));
        return;
    }

    if (!m_appliedAwards.TryRemember(acMessage.AwardId))
    {
        spdlog::debug("Ignored duplicate progression award {}.", acMessage.AwardId);
        return;
    }

    // AddSkillExperience calls Skyrim's original routine through its trampoline
    // and applies the existing ScopedExperienceOverride. The server controls
    // the award; the client only applies the already validated amount locally.
    PlayerCharacter::Get()->AddSkillExperience(*actorValue, acMessage.Experience);
    spdlog::debug("Applied progression award {} for character {}.", acMessage.AwardId, acMessage.CharacterId);
    m_dispatcher.trigger(ProgressionAwardAppliedEvent{
        acMessage.AwardId,
        acMessage.CharacterId,
        acMessage.Skill,
        acMessage.Experience,
        acMessage.Reason});
}

void ProgressionService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_appliedAwards.Clear();
}
