#include <Services/MagicService.h>

#include <Components.h>
#include <GameServer.h>
#include <Services/AddTargetAuthorityPolicy.h>
#include <World.h>

#include <Messages/SpellCastRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/AddTargetRequest.h>

#include <Messages/NotifySpellCast.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyRemoveSpell.h>

#include <cmath>

MagicService::MagicService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_spellCastConnection = aDispatcher.sink<PacketEvent<SpellCastRequest>>().connect<&MagicService::OnSpellCastRequest>(this);
    m_interruptCastConnection = aDispatcher.sink<PacketEvent<InterruptCastRequest>>().connect<&MagicService::OnInterruptCastRequest>(this);
    m_addTargetConnection = aDispatcher.sink<PacketEvent<AddTargetRequest>>().connect<&MagicService::OnAddTargetRequest>(this);
    m_removeSpellConnection = aDispatcher.sink<PacketEvent<RemoveSpellRequest>>().connect<&MagicService::OnRemoveSpellRequest>(this);
}

void MagicService::OnSpellCastRequest(const PacketEvent<SpellCastRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    if (message.CastingSource < 0 || message.CastingSource >= 4)
        return;

    const auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto it = characterView.find(static_cast<entt::entity>(message.CasterId));
    if (it == characterView.end() || !characterView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
        return;

    NotifySpellCast notify;
    notify.CasterId = message.CasterId;
    notify.SpellFormId = message.SpellFormId;
    notify.CastingSource = message.CastingSource;
    notify.IsDualCasting = message.IsDualCasting;
    notify.DesiredTarget = message.DesiredTarget;
    notify.OwnershipEpoch = message.OwnershipEpoch;

    const auto entity = static_cast<entt::entity>(message.CasterId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnInterruptCastRequest(const PacketEvent<InterruptCastRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    if (message.CastingSource < 0 || message.CastingSource >= 4)
        return;

    const auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto it = characterView.find(static_cast<entt::entity>(message.CasterId));
    if (it == characterView.end() || !characterView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
        return;

    NotifyInterruptCast notify;
    notify.CasterId = message.CasterId;
    notify.CastingSource = message.CastingSource;
    notify.OwnershipEpoch = message.OwnershipEpoch;

    const auto entity = static_cast<entt::entity>(message.CasterId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnAddTargetRequest(const PacketEvent<AddTargetRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    if (!std::isfinite(message.Magnitude))
        return;

    const auto targetEntity = static_cast<entt::entity>(message.TargetId);
    const bool targetExists = m_world.valid(targetEntity) && m_world.all_of<CharacterComponent>(targetEntity);
    const auto* targetOwner = targetExists ? m_world.try_get<OwnerComponent>(targetEntity) : nullptr;
    const bool targetHasOwner = targetOwner && targetOwner->GetOwner();
    const bool senderOwnsTarget = targetHasOwner && targetOwner->GetOwner() == acMessage.pPlayer;

    const bool casterIdProvided = message.CasterId != 0;
    bool casterExists = false;
    bool casterHasOwner = false;
    bool senderOwnsCaster = false;
    if (casterIdProvided)
    {
        const auto casterEntity = static_cast<entt::entity>(message.CasterId);
        casterExists = m_world.valid(casterEntity) && m_world.all_of<CharacterComponent>(casterEntity);
        const auto* casterOwner = casterExists ? m_world.try_get<OwnerComponent>(casterEntity) : nullptr;
        casterHasOwner = casterOwner && casterOwner->GetOwner();
        senderOwnsCaster = casterHasOwner && casterOwner->GetOwner() == acMessage.pPlayer;
    }

    if (!AddTargetAuthorityPolicy::IsAuthorized(
            targetExists, targetHasOwner, senderOwnsTarget,
            casterIdProvided, casterExists, casterHasOwner, senderOwnsCaster))
        return;

    NotifyAddTarget notify;
    notify.TargetId = message.TargetId;
    notify.CasterId = message.CasterId;
    notify.SpellId = message.SpellId;
    notify.EffectId = message.EffectId;
    notify.Magnitude = message.Magnitude;
    notify.IsDualCasting = message.IsDualCasting;
    notify.ApplyHealPerkBonus = message.ApplyHealPerkBonus;
    notify.ApplyStaminaPerkBonus = message.ApplyStaminaPerkBonus;

    if (!GameServer::Get()->SendToPlayersInRange(notify, targetEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnRemoveSpellRequest(const PacketEvent<RemoveSpellRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    
    NotifyRemoveSpell notify;
    notify.TargetId = message.TargetId;
    notify.SpellId = message.SpellId;

    //spdlog::info(__FUNCTION__ ": TargetId: {}, Spell baseId: {}", notify.TargetId, notify.SpellId.BaseId);

    const auto entity = static_cast<entt::entity>(message.TargetId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
