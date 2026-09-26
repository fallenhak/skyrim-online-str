#include <Components.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/RequestDeathStateChange.h>
#include <Services/ActorValueService.h>
#include <Services/DropLog.h>
#include <Services/ActorHealthChangePolicy.h>
#include <Services/ActorNonOwnerDamagePolicy.h>
#include <Services/SessionService.h>
#include <Services/CanonicalCreatureDeathPolicy.h>
#include <Services/FurnitureUsePolicy.h>
#include <Events/AcceptedCanonicalHealthDecreaseEvent.h>
#include <Events/AcceptedCanonicalCreatureDeathEvent.h>
#include <World.h>
#include <GameServer.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Services/ActorValueMutationPolicy.h>
#include <Services/CorpseRetentionPolicy.h>
#include <Services/InventoryInteractionPolicy.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
uint32_t CurrentEpochOf(const World& acWorld, const entt::entity aEntity) noexcept
{
    const auto* const pOwner = acWorld.valid(aEntity) ? acWorld.try_get<OwnerComponent>(aEntity) : nullptr;
    return pOwner ? pOwner->OwnershipEpoch : 0;
}
} // namespace

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
    {
        DropLog::Info("actor values: actor not found or not owner", "player {:X}, actor {:X}: {}, epoch {} (current {})",
            acMessage.pPlayer->GetId(), message.Id, it == actorValuesView.end() ? "actor not found" : "not owner", message.OwnershipEpoch,
            CurrentEpochOf(m_world, static_cast<entt::entity>(message.Id)));
        return;
    }

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
    {
        DropLog::Info("actor max values: actor not found or not owner", "player {:X}, actor {:X}: {}, epoch {} (current {})",
            acMessage.pPlayer->GetId(), message.Id, it == actorValuesView.end() ? "actor not found" : "not owner", message.OwnershipEpoch,
            CurrentEpochOf(m_world, static_cast<entt::entity>(message.Id)));
        return;
    }

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

bool ActorValueService::IsAcceptedNonOwnerDamage(const PacketEvent<RequestHealthChangeBroadcast>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    const auto entity = static_cast<entt::entity>(message.Id);

    const auto* pOwner = m_world.try_get<OwnerComponent>(entity);
    const auto* pCharacter = m_world.try_get<CharacterComponent>(entity);
    const auto* pCell = m_world.try_get<CellIdComponent>(entity);
    const bool entityExists = m_world.valid(entity) && pOwner && pCharacter && pCell;

    // Both the character flag and the trusted population identity mark player characters.
    const auto* pIdentity = m_world.try_get<ActorPopulationIdentityComponent>(entity);
    const bool targetIsPlayer =
        entityExists && (pCharacter->IsPlayer() || (pIdentity && pIdentity->Source == ActorPopulationIdentitySource::kPlayer));

    const bool senderInWorld = m_world.GetSessionService().CanProcessGameplay(acMessage.pPlayer->GetConnectionId());
    const bool senderInRange = entityExists && acMessage.pPlayer->GetCellComponent().IsInRange(*pCell, pCharacter->IsDragon());

    const uint32_t currentEpoch = entityExists ? pOwner->OwnershipEpoch : 0;
    const auto result = ActorNonOwnerDamagePolicy::Evaluate(
        entityExists, entityExists && pCharacter->IsDead(),
        targetIsPlayer, senderInWorld, senderInRange,
        currentEpoch, message.OwnershipEpoch, message.DeltaHealth);

    if (result == ActorNonOwnerDamagePolicy::Result::kAccepted)
        return true;

    DropLog::Info(ActorNonOwnerDamagePolicy::ToString(result),
        "player {:X}, actor {:X}, delta {}, epoch {} (current {}), in world {}, in range {}", acMessage.pPlayer->GetId(), message.Id,
        message.DeltaHealth, message.OwnershipEpoch, currentEpoch, senderInWorld, senderInRange);
    return false;
}

void ActorValueService::OnHealthChangeBroadcast(const PacketEvent<RequestHealthChangeBroadcast>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();
    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    const bool entityExists = it != actorValuesView.end();
    const bool isCurrentOwner = entityExists && actorValuesView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch);
    // The non-owner path below dereferences the same view entity, so it needs the entity too.
    if (!entityExists)
    {
        DropLog::Info("health change: actor not found", "player {:X}, actor {:X}, delta {}",
            acMessage.pPlayer->GetId(), message.Id, message.DeltaHealth);
        return;
    }

    // IsAcceptedNonOwnerDamage logs its own drop reason.
    if (!ActorHealthChangePolicy::IsAuthorized(entityExists, entityExists, isCurrentOwner, message.OwnershipEpoch) && !IsAcceptedNonOwnerDamage(acMessage))
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
    {
        DropLog::Info("health change: delta not applicable", "player {:X}, actor {:X}, delta {}",
            acMessage.pPlayer->GetId(), message.Id, message.DeltaHealth);
        return;
    }

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
    {
        DropLog::Info("death state: actor not found or not owner", "player {:X}, actor {:X}: {}, dead {}, epoch {} (current {})",
            acMessage.pPlayer->GetId(), message.Id, it == characterView.end() ? "actor not found" : "not owner", message.IsDead,
            message.OwnershipEpoch, CurrentEpochOf(m_world, static_cast<entt::entity>(message.Id)));
        return;
    }

    auto& characterComponent = characterView.get<CharacterComponent>(*it);
    const bool wasDead = characterComponent.IsDead();
    const bool hasCorpseMarker = m_world.all_of<CorpseRetentionComponent>(*it);
    if (!CorpseRetentionPolicy::AllowsDeathStateChange(hasCorpseMarker, wasDead, message.IsDead))
        return;

    if (message.IsSettledPosition && !message.IsDead)
        return;

    if (wasDead == message.IsDead && !message.IsSettledPosition)
        return;

    const auto entity = *it;
    if (wasDead != message.IsDead)
    {
        characterComponent.SetDead(message.IsDead);

        if (auto* const pAnimationComponent = m_world.try_get<AnimationComponent>(entity))
            FurnitureUsePolicy::ClearReservation(
                pAnimationComponent->FurnitureUseTargetId,
                pAnimationComponent->RejectedFurnitureTargetId,
                pAnimationComponent->HasEnteredFurniture,
                pAnimationComponent->RejectedFurnitureSawActiveState);

        const auto* const pPopulationIdentity = m_world.try_get<ActorPopulationIdentityComponent>(entity);
        auto* const pLifecycle = m_world.try_get<ActorLifecycleComponent>(entity);
        if (CanonicalCreatureDeathPolicy::TryAcceptTransition(
                wasDead, &characterComponent, pPopulationIdentity, pLifecycle))
        {
            m_dispatcher.trigger(AcceptedCanonicalCreatureDeathEvent{
                World::ToInteger(entity), pLifecycle->GetGeneration()});
        }
    }

    // The owner's settled corpse, with what the engine added at death, becomes the corpse's record. From
    // here on a retained corpse's contents change only through container transfers.
    bool recordedCorpseContents = false;
    if (message.IsSettledPosition && message.IsDead)
    {
        auto* const pCorpse = m_world.try_get<CorpseRetentionComponent>(entity);
        auto* const pInventory = m_world.try_get<InventoryComponent>(entity);
        const bool cValidPayload = std::all_of(message.CorpseContents.Entries.begin(), message.CorpseContents.Entries.end(),
            [](const auto& acEntry) { return InventoryInteractionPolicy::HasValidItemPayload(acEntry); });
        if (pInventory && cValidPayload && (!pCorpse || !pCorpse->OwnerSeedClosed))
        {
            pInventory->Content = message.CorpseContents;
            if (pCorpse)
                pCorpse->OwnerSeedClosed = true;
            recordedCorpseContents = true;
            spdlog::info("[CorpseSync] recorded corpse actor {:X} contents from its owner: {} item(s)", message.Id, message.CorpseContents.Entries.size());
        }
        else if (!cValidPayload)
            spdlog::warn("[CorpseSync] corpse actor {:X}: owner's contents rejected as malformed", message.Id);
    }

    const auto* const pMovement = message.IsSettledPosition ? m_world.try_get<MovementComponent>(entity) : nullptr;
    if (message.IsSettledPosition && !pMovement)
        spdlog::warn("[CorpseSync] cannot relay settled position for actor {:X}: movement state is unavailable", message.Id);

    NotifyDeathStateChange notify;
    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.Id = message.Id;
    notify.IsDead = message.IsDead;
    notify.IsSettledPosition = pMovement != nullptr;
    if (pMovement)
        notify.Position = pMovement->Position;
    if (pMovement && message.IsDead)
    {
        if (const auto* pInventory = m_world.try_get<InventoryComponent>(entity); pInventory && (recordedCorpseContents || m_world.all_of<CorpseRetentionComponent>(entity)))
        {
            notify.HasCorpseContents = true;
            notify.CorpseContents = pInventory->Content;
        }
    }

    if (message.IsDead && pMovement)
    {
        spdlog::info(
            "[CorpseSync] relaying settled position actor {:X} epoch {} at ({:.0f}, {:.0f}, {:.0f})",
            message.Id, message.OwnershipEpoch, pMovement->Position.x, pMovement->Position.y, pMovement->Position.z);
    }
    else
    {
        spdlog::debug("Updating death state {:x}:{}", message.Id, message.IsDead);
    }

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
