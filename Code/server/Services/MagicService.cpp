#include <Services/MagicService.h>

#include <Components.h>
#include <GameServer.h>
#include <Services/AddTargetAuthorityPolicy.h>
#include <Services/DropLog.h>
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
    {
        DropLog::Info("spell cast: invalid casting source", "player {:X}, caster {:X}, source {}",
            acMessage.pPlayer->GetId(), message.CasterId, message.CastingSource);
        return;
    }

    const auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto it = characterView.find(static_cast<entt::entity>(message.CasterId));
    if (it == characterView.end() || !characterView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
    {
        DropLog::Info("spell cast: caster not found or not owner", "player {:X}, caster {:X}: {}, epoch {} (current {})",
            acMessage.pPlayer->GetId(), message.CasterId, it == characterView.end() ? "caster not found" : "not owner", message.OwnershipEpoch,
            it == characterView.end() ? 0u : characterView.get<OwnerComponent>(*it).OwnershipEpoch);
        return;
    }

    if (message.DesiredTarget != 0)
    {
        const auto targetEntity = static_cast<entt::entity>(message.DesiredTarget);
        if (!m_world.valid(targetEntity) ||
            (!m_world.all_of<CharacterComponent>(targetEntity) && !m_world.all_of<ObjectComponent>(targetEntity)))
        {
            DropLog::Info("spell cast: desired target not found", "player {:X}, caster {:X}, target {:X}",
                acMessage.pPlayer->GetId(), message.CasterId, message.DesiredTarget);
            return;
        }
    }

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
    {
        DropLog::Info("interrupt cast: invalid casting source", "player {:X}, caster {:X}, source {}",
            acMessage.pPlayer->GetId(), message.CasterId, message.CastingSource);
        return;
    }

    const auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto it = characterView.find(static_cast<entt::entity>(message.CasterId));
    if (it == characterView.end() || !characterView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
    {
        DropLog::Info("interrupt cast: caster not found or not owner", "player {:X}, caster {:X}: {}, epoch {}",
            acMessage.pPlayer->GetId(), message.CasterId, it == characterView.end() ? "caster not found" : "not owner", message.OwnershipEpoch);
        return;
    }

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
    {
        DropLog::Info("add target: magnitude not finite", "player {:X}, target {:X}, spell {:X}",
            acMessage.pPlayer->GetId(), message.TargetId, message.SpellId.LogFormat());
        return;
    }

    const auto targetEntity = static_cast<entt::entity>(message.TargetId);
    const bool targetExists = m_world.valid(targetEntity) && m_world.all_of<CharacterComponent>(targetEntity);
    const auto* targetOwner = targetExists ? m_world.try_get<OwnerComponent>(targetEntity) : nullptr;
    const bool targetHasOwner = targetOwner && targetOwner->GetOwner();
    const bool senderOwnsTarget = targetHasOwner && targetOwner->GetOwner() == acMessage.pPlayer;
    const uint32_t currentTargetOwnershipEpoch = targetOwner ? targetOwner->OwnershipEpoch : 0;

    const bool casterIdProvided = message.CasterId != 0;
    bool casterExists = false;
    bool casterHasOwner = false;
    bool senderOwnsCaster = false;
    uint32_t currentCasterOwnershipEpoch = 0;
    if (casterIdProvided)
    {
        const auto casterEntity = static_cast<entt::entity>(message.CasterId);
        casterExists = m_world.valid(casterEntity) && m_world.all_of<CharacterComponent>(casterEntity);
        const auto* casterOwner = casterExists ? m_world.try_get<OwnerComponent>(casterEntity) : nullptr;
        casterHasOwner = casterOwner && casterOwner->GetOwner();
        senderOwnsCaster = casterHasOwner && casterOwner->GetOwner() == acMessage.pPlayer;
        currentCasterOwnershipEpoch = casterOwner ? casterOwner->OwnershipEpoch : 0;
    }

    if (!AddTargetAuthorityPolicy::IsAuthorized(
            targetExists, targetHasOwner, senderOwnsTarget, message.TargetOwnershipEpoch, currentTargetOwnershipEpoch,
            casterIdProvided, casterExists, casterHasOwner, senderOwnsCaster, message.CasterOwnershipEpoch, currentCasterOwnershipEpoch))
    {
        DropLog::Info("add target: not authorized", "player {:X}; target {:X} exists {}, owned by sender {}, epoch {} (current {}); caster {:X} exists {}, owned by sender {}, epoch {} (current {})",
            acMessage.pPlayer->GetId(), message.TargetId, targetExists, senderOwnsTarget, message.TargetOwnershipEpoch, currentTargetOwnershipEpoch,
            message.CasterId, casterExists, senderOwnsCaster, message.CasterOwnershipEpoch, currentCasterOwnershipEpoch);
        return;
    }

    NotifyAddTarget notify;
    notify.TargetId = message.TargetId;
    notify.CasterId = message.CasterId;
    notify.SpellId = message.SpellId;
    notify.EffectId = message.EffectId;
    notify.Magnitude = message.Magnitude;
    notify.IsDualCasting = message.IsDualCasting;
    notify.ApplyHealPerkBonus = message.ApplyHealPerkBonus;
    notify.ApplyStaminaPerkBonus = message.ApplyStaminaPerkBonus;
    notify.TargetOwnershipEpoch = message.TargetOwnershipEpoch;
    notify.CasterOwnershipEpoch = message.CasterOwnershipEpoch;

    if (!GameServer::Get()->SendToPlayersInRange(notify, targetEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnRemoveSpellRequest(const PacketEvent<RemoveSpellRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;

    const auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto it = characterView.find(static_cast<entt::entity>(message.TargetId));
    if (it == characterView.end() ||
        !characterView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.GetSender(), message.OwnershipEpoch))
    {
        DropLog::Info("remove spell: target not found or not owner", "player {:X}, target {:X}: {}, epoch {}",
            acMessage.pPlayer->GetId(), message.TargetId, it == characterView.end() ? "target not found" : "not owner", message.OwnershipEpoch);
        return;
    }

    NotifyRemoveSpell notify;
    notify.TargetId = message.TargetId;
    notify.SpellId = message.SpellId;
    notify.OwnershipEpoch = message.OwnershipEpoch;

    //spdlog::info(__FUNCTION__ ": TargetId: {}, Spell baseId: {}", notify.TargetId, notify.SpellId.BaseId);

    const auto entity = static_cast<entt::entity>(message.TargetId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
