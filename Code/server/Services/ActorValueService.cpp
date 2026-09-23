#include <Components.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/RequestDeathStateChange.h>
#include <Services/ActorValueService.h>
#include <Services/ActorHealthChangePolicy.h>
#include <Events/AcceptedCanonicalHealthDecreaseEvent.h>
#include <World.h>
#include <GameServer.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Services/ActorValueMutationPolicy.h>

#include <cmath>
#include <utility>

namespace
{
void EmitCanonicalHealthDecrease(World& aWorld, entt::dispatcher& aDispatcher, const entt::entity aEntity,
                                 const float aPreviousHealth, const float aCurrentHealth) noexcept
{
    if (!ActorHealthChangePolicy::IsCanonicalDecrease(aPreviousHealth, aCurrentHealth))
        return;

    const auto* const pLifecycle = aWorld.try_get<ActorLifecycleComponent>(aEntity);
    if (pLifecycle && pLifecycle->IsValid())
    {
        aDispatcher.trigger(AcceptedCanonicalHealthDecreaseEvent{
            World::ToInteger(aEntity), pLifecycle->GetGeneration()});
    }
}
}

ActorValueService::ActorValueService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_updateHealthConnection = aDispatcher.sink<PacketEvent<RequestActorValueChanges>>().connect<&ActorValueService::OnActorValueChanges>(this);
    m_updateMaxValueConnection = aDispatcher.sink<PacketEvent<RequestActorMaxValueChanges>>().connect<&ActorValueService::OnActorMaxValueChanges>(this);
    m_updateDeltaHealthConnection = aDispatcher.sink<PacketEvent<RequestHealthChangeBroadcast>>().connect<&ActorValueService::OnHealthChangeBroadcast>(this);
    m_deathStateConnection = aDispatcher.sink<PacketEvent<RequestDeathStateChange>>().connect<&ActorValueService::OnDeathStateChange>(this);
}

void ActorValueService::OnActorValueChanges(const PacketEvent<RequestActorValueChanges>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    if (it == actorValuesView.end() || !actorValuesView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
        return;

    auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
    auto& actorValues = actorValuesComponent.CurrentActorValues.ActorValuesList;
    const auto healthIt = actorValues.find(ActorHealthChangePolicy::kHealthActorValue);
    const bool hadCanonicalHealth = healthIt != actorValues.end();
    const float previousHealth = hadCanonicalHealth ? healthIt.value() : 0.f;
    TiltedPhoques::Map<uint32_t, float> acceptedValues;
    for (const auto& [id, value] : message.Values)
    {
        if (!ActorValueMutationPolicy::IsValidIndexAndValue(id, value, ActorValueMutationPolicy::kActorValueCount))
            continue;

        auto currentValueIt = actorValues.find(id);
        if (currentValueIt == actorValues.end())
            continue;

        currentValueIt.value() = value;
        acceptedValues.emplace(id, value);
    }

    if (acceptedValues.empty())
        return;

    const auto acceptedHealthIt = acceptedValues.find(ActorHealthChangePolicy::kHealthActorValue);
    if (hadCanonicalHealth && acceptedHealthIt != acceptedValues.end())
        EmitCanonicalHealthDecrease(m_world, m_dispatcher, *it, previousHealth, acceptedHealthIt.value());

    NotifyActorValueChanges notify;
    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.Id = acMessage.Packet.Id;
    notify.Values = std::move(acceptedValues);

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void ActorValueService::OnActorMaxValueChanges(const PacketEvent<RequestActorMaxValueChanges>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    if (it == actorValuesView.end() || !actorValuesView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
        return;

    auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
    TiltedPhoques::Map<uint32_t, float> acceptedValues;
    for (const auto& [id, value] : message.Values)
    {
        if (!ActorValueMutationPolicy::IsValidIndexAndValue(id, value, ActorValueMutationPolicy::kActorValueCount))
            continue;

        auto currentValueIt = actorValuesComponent.CurrentActorValues.ActorMaxValuesList.find(id);
        if (currentValueIt == actorValuesComponent.CurrentActorValues.ActorMaxValuesList.end())
            continue;

        currentValueIt.value() = value;
        acceptedValues.emplace(id, value);
    }

    if (acceptedValues.empty())
        return;

    NotifyActorMaxValueChanges notify;
    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.Id = message.Id;
    notify.Values = std::move(acceptedValues);

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void ActorValueService::OnHealthChangeBroadcast(const PacketEvent<RequestHealthChangeBroadcast>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();
    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    const bool entityExists = it != actorValuesView.end();
    const bool isCurrentOwner = entityExists && actorValuesView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch);
    if (!ActorHealthChangePolicy::IsAuthorized(entityExists, entityExists, isCurrentOwner, message.OwnershipEpoch))
        return;

    auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
    auto& actorValues = actorValuesComponent.CurrentActorValues.ActorValuesList;
    const auto healthIt = actorValues.find(ActorHealthChangePolicy::kHealthActorValue);
    const bool hadCanonicalHealth = healthIt != actorValues.end();
    const float previousHealth = hadCanonicalHealth ? healthIt.value() : 0.f;

    // DeltaHealth is signed: damage is negative and healing is positive. Only
    // the resulting canonical value can produce a decrease signal; the
    // submitted delta is not forwarded as combat evidence.
    if (!ActorHealthChangePolicy::TryApplySignedDelta(actorValues, message.DeltaHealth))
        return;

    const auto updatedHealthIt = actorValues.find(ActorHealthChangePolicy::kHealthActorValue);
    if (hadCanonicalHealth && updatedHealthIt != actorValues.end())
        EmitCanonicalHealthDecrease(m_world, m_dispatcher, *it, previousHealth, updatedHealthIt.value());

    NotifyHealthChangeBroadcast notify;
    notify.Id = message.Id;
    notify.DeltaHealth = message.DeltaHealth;
    notify.OwnershipEpoch = message.OwnershipEpoch;

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void ActorValueService::OnDeathStateChange(const PacketEvent<RequestDeathStateChange>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto characterView = m_world.view<CharacterComponent, OwnerComponent>();

    const auto it = characterView.find(static_cast<entt::entity>(message.Id));

    if (it == characterView.end() || !characterView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
        return;

    auto& characterComponent = characterView.get<CharacterComponent>(*it);
    characterComponent.SetDead(message.IsDead);
    spdlog::debug("Updating death state {:x}:{}", message.Id, message.IsDead);

    NotifyDeathStateChange notify;
    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.Id = message.Id;
    notify.IsDead = message.IsDead;

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
