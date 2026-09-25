#include <Services/ObjectService.h>

#include <GameServer.h>
#include <World.h>
#include <Components.h>
#include <Services/ObjectInteractionPolicy.h>
#include <Services/PresentationAuthorityPolicy.h>

#include <Events/PlayerLeaveCellEvent.h>
#include <Events/UpdateEvent.h>

#include <Messages/ActivateRequest.h>
#include <Messages/TakeWorldItemRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/NotifyObjectHarvested.h>
#include <Messages/NotifyWorldItemTaken.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/NotifyLockChange.h>
#include <Messages/AssignObjectsRequest.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Messages/NotifyScriptAnimation.h>

#include <cstddef>

namespace
{
bool IsPlayerInObjectRange(const Player& acPlayer, const CellIdComponent& acObjectCell) noexcept
{
    const auto& playerCell = acPlayer.GetCellComponent();
    return ObjectInteractionPolicy::IsInSenderRange(
        playerCell.Cell, playerCell.WorldSpaceId, playerCell.CenterCoords,
        acObjectCell.Cell, acObjectCell.WorldSpaceId, acObjectCell.CenterCoords);
}

template <typename TMessage>
std::size_t NotifyObjectPeersInRange(World& aWorld, const TMessage& acMessage, const CellIdComponent& acObjectCell, const Player* apExcluded) noexcept
{
    std::size_t notifiedPlayers = 0;
    for (Player* pPlayer : aWorld.GetPlayerManager())
    {
        if (pPlayer == apExcluded || !IsPlayerInObjectRange(*pPlayer, acObjectCell))
            continue;

        pPlayer->Send(acMessage);
        ++notifiedPlayers;
    }

    return notifiedPlayers;
}
} // namespace

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_leaveCellConnection = aDispatcher.sink<PlayerLeaveCellEvent>().connect<&ObjectService::OnPlayerLeaveCellEvent>(this);
    m_assignObjectConnection = aDispatcher.sink<PacketEvent<AssignObjectsRequest>>().connect<&ObjectService::OnAssignObjectsRequest>(this);
    m_activateConnection = aDispatcher.sink<PacketEvent<ActivateRequest>>().connect<&ObjectService::OnActivate>(this);
    m_takeWorldItemConnection = aDispatcher.sink<PacketEvent<TakeWorldItemRequest>>().connect<&ObjectService::OnTakeWorldItem>(this);
    m_lockChangeConnection = aDispatcher.sink<PacketEvent<LockChangeRequest>>().connect<&ObjectService::OnLockChange>(this);
    m_scriptAnimationConnection = aDispatcher.sink<PacketEvent<ScriptAnimationRequest>>().connect<&ObjectService::OnScriptAnimationRequest>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&ObjectService::OnUpdate>(this);
}

// TODO(cosideci): the cell handling of objects need to be revamped.
// We already store the location and worldspace of the mod through CellIdComponent.
// Clients need a message saying the entity was destroyed.
void ObjectService::OnPlayerLeaveCellEvent(const PlayerLeaveCellEvent&) noexcept
{
    PruneUnobservedObjects();
}

void ObjectService::PruneUnobservedObjects() noexcept
{
    auto objectView = m_world.view<ObjectComponent, CellIdComponent>();
    Vector<entt::entity> toDestroy;

    for (const auto entity : objectView)
    {
        const auto& objectComponent = objectView.get<ObjectComponent>(entity);
        const auto& objectCell = objectView.get<CellIdComponent>(entity);

        if (ObjectInteractionPolicy::ShouldRetainWorldState(
                objectComponent.IsLootTaken, objectComponent.IsHarvested, objectComponent.HarvestRespawnAtTick, m_tick))
            continue;

        bool hasNearbyPlayer = false;
        for (Player* pPlayer : m_world.GetPlayerManager())
        {
            if (IsPlayerInObjectRange(*pPlayer, objectCell))
            {
                hasNearbyPlayer = true;
                break;
            }
        }
        if (!hasNearbyPlayer)
            toDestroy.push_back(entity);
    }

    for (const auto entity : toDestroy)
        m_world.destroy(entity);
}

// NOTE: this whole system kinda relies on all objects in a cell being static.
// This is fine for containers and doors, but if this system is expanded, think of temporaries.
void ObjectService::OnAssignObjectsRequest(const PacketEvent<AssignObjectsRequest>& acMessage) noexcept
{
    auto view = m_world.view<FormIdComponent, ObjectComponent, CellIdComponent, InventoryComponent>();
    const auto& senderCell = acMessage.pPlayer->GetCellComponent();

    AssignObjectsResponse response;

    for (const ObjectData& object : acMessage.Packet.Objects)
    {
        if (!object.Id)
            continue;

        const auto iter = std::find_if(
            std::begin(view), std::end(view),
            [view, id = object.Id](auto entity)
            {
                const auto& formIdComponent = view.get<FormIdComponent>(entity);
                return formIdComponent.Id == id;
            });

        if (iter != std::end(view))
        {
            const auto& objectCell = view.get<CellIdComponent>(*iter);
            if (!ObjectInteractionPolicy::CanInteract(
                    object.CellId, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
                    objectCell.Cell, objectCell.WorldSpaceId, objectCell.CenterCoords))
                continue;

            ObjectData objectData;
            objectData.ServerId = World::ToInteger(*iter);

            auto& formIdComponent = view.get<FormIdComponent>(*iter);
            objectData.Id = formIdComponent.Id;

            auto& objectComponent = view.get<ObjectComponent>(*iter);
            objectData.IsStateUntrusted = !objectComponent.HasTrustedState;
            objectData.IsHarvestable = objectComponent.IsHarvestable;
            objectData.IsHarvestItem = objectComponent.IsHarvestItem;
            objectData.IsHarvested = objectComponent.IsHarvested;
            objectData.IsOpenLoot = objectComponent.IsOpenLoot;
            objectData.IsLootTaken = objectComponent.IsLootTaken;
            objectData.IsDoor = objectComponent.IsDoor;
            objectData.IsDoorStateKnown = objectComponent.Door.IsKnown;
            objectData.IsDoorOpen = objectComponent.Door.IsOpen;
            objectData.IsActivator = objectComponent.IsActivator;
            objectData.ActivationCount = objectComponent.Activator.ActivationCount;
            if (objectComponent.HasTrustedState)
            {
                objectData.CurrentLockData = objectComponent.CurrentLockData;
                objectData.CurrentInventory = view.get<InventoryComponent>(*iter).Content;
            }

            response.Objects.push_back(objectData);
        }
        else
        {
            if (!ObjectInteractionPolicy::CanDiscover(
                    object.Id, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
                    object.CellId, object.WorldSpaceId, object.CurrentCoords))
                continue;

            const auto cEntity = m_world.create();

            m_world.emplace<FormIdComponent>(cEntity, object.Id);

            auto& objectComponent = m_world.emplace<ObjectComponent>(cEntity, acMessage.pPlayer);
            objectComponent.IsHarvestable = object.IsHarvestable;
            objectComponent.IsHarvestItem = object.IsHarvestable && object.IsHarvestItem;
            objectComponent.IsOpenLoot = object.IsOpenLoot && !object.IsHarvestable;
            objectComponent.IsDoor = object.IsDoor;
            objectComponent.IsActivator = object.IsActivator && !object.IsDoor && !object.IsHarvestable;

            m_world.emplace<CellIdComponent>(cEntity, object.CellId, object.WorldSpaceId, object.CurrentCoords);
            m_world.emplace<InventoryComponent>(cEntity);

            ObjectData objectData;
            objectData.Id = object.Id;
            objectData.ServerId = World::ToInteger(cEntity);
            objectData.IsStateUntrusted = true;
            objectData.IsHarvestable = object.IsHarvestable;
            objectData.IsHarvestItem = objectComponent.IsHarvestItem;
            objectData.IsOpenLoot = objectComponent.IsOpenLoot;
            objectData.IsDoor = object.IsDoor;
            objectData.IsActivator = objectComponent.IsActivator;

            response.Objects.push_back(objectData);
        }
    }

    if (!response.Objects.empty())
        acMessage.pPlayer->Send(response);
}

void ObjectService::OnTakeWorldItem(const PacketEvent<TakeWorldItemRequest>& acMessage) noexcept
{
    const auto& packet = acMessage.Packet;
    const auto objectView = m_world.view<FormIdComponent, ObjectComponent, CellIdComponent>();
    const auto objectIt = std::find_if(
        objectView.begin(), objectView.end(),
        [objectView, id = packet.Id](const auto entity) { return objectView.get<FormIdComponent>(entity).Id == id; });
    if (objectIt == objectView.end())
        return;

    const auto& senderCell = acMessage.pPlayer->GetCellComponent();
    const auto& objectCell = objectView.get<CellIdComponent>(*objectIt);
    auto& objectComponent = objectView.get<ObjectComponent>(*objectIt);

    const auto activatorEntity = static_cast<entt::entity>(packet.ActivatorId);
    const auto activatorView = m_world.view<CharacterComponent, OwnerComponent, CellIdComponent>();
    const auto activatorIt = activatorView.find(activatorEntity);
    const bool activatorExists = activatorIt != activatorView.end();
    const bool ownedBySender = activatorExists && activatorView.get<OwnerComponent>(*activatorIt).GetOwner() == acMessage.pPlayer;
    if (!activatorExists)
        return;

    const auto& activatorCell = activatorView.get<CellIdComponent>(*activatorIt);
    std::size_t notifiedPlayers = 0;
    const bool taken = ObjectInteractionPolicy::TryTakeWorldItem(
        objectComponent.IsOpenLoot, objectComponent.IsLootTaken, activatorExists, ownedBySender,
        packet.CellId, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
        activatorCell.Cell, activatorCell.WorldSpaceId, activatorCell.CenterCoords,
        objectCell.Cell, objectCell.WorldSpaceId, objectCell.CenterCoords,
        [&]
        {
            NotifyWorldItemTaken notify{};
            notify.Id = packet.Id;
            notifiedPlayers = NotifyObjectPeersInRange(m_world, notify, objectCell, acMessage.pPlayer);
        });

    if (taken)
        spdlog::info("[World] loot {:X}:{:X} taken; notified {} peer(s)", packet.Id.ModId, packet.Id.BaseId, notifiedPlayers);
    else
        spdlog::debug("World loot pickup {:X}:{:X} rejected (already taken or not authorized/in range)", packet.Id.ModId, packet.Id.BaseId);
}

void ObjectService::OnActivate(const PacketEvent<ActivateRequest>& acMessage) const noexcept
{
    const auto& packet = acMessage.Packet;
    if (!ObjectInteractionPolicy::IsValidOpenState(packet.PreActivationOpenState))
        return;

    const auto objectView = m_world.view<FormIdComponent, ObjectComponent, CellIdComponent>();
    const auto objectIt = std::find_if(
        objectView.begin(), objectView.end(),
        [objectView, id = packet.Id](const auto entity)
        {
            return objectView.get<FormIdComponent>(entity).Id == id;
        });
    if (objectIt == objectView.end())
        return;

    const auto& senderCell = acMessage.pPlayer->GetCellComponent();
    const auto& objectCell = objectView.get<CellIdComponent>(*objectIt);
    auto& objectComponent = objectView.get<ObjectComponent>(*objectIt);

    const auto activatorEntity = static_cast<entt::entity>(packet.ActivatorId);
    const auto activatorView = m_world.view<CharacterComponent, OwnerComponent, CellIdComponent>();
    const auto activatorIt = activatorView.find(activatorEntity);
    const bool activatorExists = activatorIt != activatorView.end();
    const bool ownedBySender = activatorExists && activatorView.get<OwnerComponent>(*activatorIt).GetOwner() == acMessage.pPlayer;
    if (!activatorExists)
        return;

    const auto& activatorCell = activatorView.get<CellIdComponent>(*activatorIt);

    if (objectComponent.IsHarvestable)
    {
        std::size_t notifiedPlayers = 0;
        const bool harvested = ObjectInteractionPolicy::TryHarvest(
            true, objectComponent.IsHarvested, activatorExists, ownedBySender,
            packet.CellId, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
            activatorCell.Cell, activatorCell.WorldSpaceId, activatorCell.CenterCoords,
            objectCell.Cell, objectCell.WorldSpaceId, objectCell.CenterCoords,
            [&]
            {
                NotifyObjectHarvested notifyHarvested;
                notifyHarvested.Id = packet.Id;
                notifyHarvested.IsHarvested = true;

                notifiedPlayers = NotifyObjectPeersInRange(m_world, notifyHarvested, objectCell, acMessage.pPlayer);
            });
        if (harvested)
        {
            objectComponent.HarvestRespawnAtTick = ObjectInteractionPolicy::HarvestRespawnTick(m_tick, objectComponent.IsHarvestItem);
            spdlog::info("[World] harvest {:X}:{:X} accepted; notified {} peer(s)", packet.Id.ModId, packet.Id.BaseId, notifiedPlayers);
        }
        else
            spdlog::info("Harvest of {:X}:{:X} rejected (already harvested or out of range)", packet.Id.ModId, packet.Id.BaseId);
        return;
    }

    if (objectComponent.IsDoor)
    {
        const bool toggled = ObjectInteractionPolicy::TryToggleDoor(
            true, objectComponent.Door, packet.PreActivationOpenState, activatorExists, ownedBySender,
            packet.CellId, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
            activatorCell.Cell, activatorCell.WorldSpaceId, activatorCell.CenterCoords,
            objectCell.Cell, objectCell.WorldSpaceId, objectCell.CenterCoords,
            [&]
            {
                NotifyActivate notifyActivate;
                notifyActivate.Id = packet.Id;
                notifyActivate.ActivatorId = packet.ActivatorId;
                notifyActivate.PreActivationOpenState = packet.PreActivationOpenState;

                for (Player* pPlayer : m_world.GetPlayerManager())
                {
                    if (pPlayer != acMessage.pPlayer && pPlayer->GetCellComponent().Cell == packet.CellId)
                        pPlayer->Send(notifyActivate);
                }
            });
        if (!toggled)
            spdlog::info("Door toggle of {:X}:{:X} rejected (stale state {} or not allowed)", packet.Id.ModId, packet.Id.BaseId, packet.PreActivationOpenState);
        return;
    }

    if (objectComponent.IsActivator)
    {
        const bool relayed = ObjectInteractionPolicy::TryRelayActivator(
            true, objectComponent.Activator, m_tick, activatorExists, ownedBySender,
            packet.CellId, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
            activatorCell.Cell, activatorCell.WorldSpaceId, activatorCell.CenterCoords,
            objectCell.Cell, objectCell.WorldSpaceId, objectCell.CenterCoords,
            [&]
            {
                NotifyActivate notifyActivate;
                notifyActivate.Id = packet.Id;
                notifyActivate.ActivatorId = packet.ActivatorId;
                notifyActivate.PreActivationOpenState = packet.PreActivationOpenState;

                for (Player* pPlayer : m_world.GetPlayerManager())
                {
                    if (pPlayer != acMessage.pPlayer && pPlayer->GetCellComponent().Cell == packet.CellId)
                        pPlayer->Send(notifyActivate);
                }
            });
        if (relayed)
            spdlog::info("[World] activator {:X}:{:X} activation #{}", packet.Id.ModId, packet.Id.BaseId, objectComponent.Activator.ActivationCount);
        else
            spdlog::info("Activator {:X}:{:X} activation rejected (cooldown or not allowed)", packet.Id.ModId, packet.Id.BaseId);
        return;
    }

    if (!ObjectInteractionPolicy::CanActivate(
            objectComponent.HasTrustedState, activatorExists, ownedBySender,
            packet.CellId, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
            activatorCell.Cell, activatorCell.WorldSpaceId, activatorCell.CenterCoords,
            objectCell.Cell, objectCell.WorldSpaceId, objectCell.CenterCoords))
        return;

    NotifyActivate notifyActivate;
    notifyActivate.Id = packet.Id;
    notifyActivate.ActivatorId = packet.ActivatorId;
    notifyActivate.PreActivationOpenState = packet.PreActivationOpenState;

    for (auto pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer != acMessage.pPlayer && pPlayer->GetCellComponent().Cell == packet.CellId)
        {
            pPlayer->Send(notifyActivate);
        }
    }
}

void ObjectService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (acEvent.Delta <= 0.f)
        return;

    m_tickAccumulator += acEvent.Delta;
    while (m_tickAccumulator >= 1.0)
    {
        m_tickAccumulator -= 1.0;
        ++m_tick;
        RespawnHarvestedObjects();
    }
}

void ObjectService::RespawnHarvestedObjects() noexcept
{
    auto view = m_world.view<FormIdComponent, ObjectComponent, CellIdComponent>();
    for (auto entity : view)
    {
        auto& objectComponent = view.get<ObjectComponent>(entity);
        if (!ObjectInteractionPolicy::IsHarvestRespawnDue(objectComponent.IsHarvested, objectComponent.HarvestRespawnAtTick, m_tick))
            continue;

        objectComponent.IsHarvested = false;
        objectComponent.HarvestRespawnAtTick = 0;

        NotifyObjectHarvested notifyRespawned;
        notifyRespawned.Id = view.get<FormIdComponent>(entity).Id;
        notifyRespawned.IsHarvested = false;

        const auto& objectCell = view.get<CellIdComponent>(entity);
        const std::size_t notifiedPlayers = NotifyObjectPeersInRange(m_world, notifyRespawned, objectCell, nullptr);

        spdlog::info("[World] harvest respawn {:X}:{:X} tick={} notified={}", notifyRespawned.Id.ModId, notifyRespawned.Id.BaseId, m_tick, notifiedPlayers);
    }

    PruneUnobservedObjects();
}

void ObjectService::OnLockChange(const PacketEvent<LockChangeRequest>& acMessage) const noexcept
{
    const auto& packet = acMessage.Packet;
    auto objectView = m_world.view<FormIdComponent, ObjectComponent, CellIdComponent>();

    const auto iter = std::find_if(
        std::begin(objectView), std::end(objectView),
        [objectView, id = packet.Id](auto entity)
        {
            const auto& formIdComponent = objectView.get<FormIdComponent>(entity);
            return formIdComponent.Id == id;
        });
    if (iter == std::end(objectView))
        return;

    const auto& senderCell = acMessage.pPlayer->GetCellComponent();
    const auto& objectCell = objectView.get<CellIdComponent>(*iter);
    if (!ObjectInteractionPolicy::CanInteract(
            packet.CellId, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
            objectCell.Cell, objectCell.WorldSpaceId, objectCell.CenterCoords))
        return;

    auto& objectComponent = objectView.get<ObjectComponent>(*iter);
    if (!ObjectInteractionPolicy::TryHandleLockChange(
        objectComponent.HasTrustedState,
        false, // LockChangeRequest contains only the client's reported outcome; no server-side resolver validates it.
        objectComponent.CurrentLockData, packet.IsLocked, packet.LockLevel,
        [&]
        {
            NotifyLockChange notifyLockChange;
            notifyLockChange.Id = packet.Id;
            notifyLockChange.IsLocked = packet.IsLocked;
            notifyLockChange.LockLevel = packet.LockLevel;

            for (Player* pPlayer : m_world.GetPlayerManager())
            {
                if (pPlayer == acMessage.pPlayer)
                    continue;

                if (pPlayer->GetCellComponent().Cell == packet.CellId)
                    pPlayer->Send(notifyLockChange);
            }
        }))
        return;
}

void ObjectService::OnScriptAnimationRequest(const PacketEvent<ScriptAnimationRequest>& acMessage) noexcept
{
    const auto& packet = acMessage.Packet;
    const auto source = static_cast<entt::entity>(packet.ServerId);
    if (!m_world.valid(source))
        return;

    const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(source);
    const auto* pCellComponent = m_world.try_get<CellIdComponent>(source);
    const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(source);
    const bool isNpcCharacter = pCharacterComponent && !pCharacterComponent->IsPlayer();
    const auto& senderCell = acMessage.pPlayer->GetCellComponent();
    const bool hasCell = pCellComponent && static_cast<bool>(*pCellComponent) && static_cast<bool>(senderCell);
    const bool isInRange = hasCell && ObjectInteractionPolicy::IsInSenderRange(
        senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
        pCellComponent->Cell, pCellComponent->WorldSpaceId, pCellComponent->CenterCoords,
        pCharacterComponent && pCharacterComponent->IsDragon());
    if (!PresentationAuthorityPolicy::CanRelayScriptAnimation(
            true, pFormIdComponent && pFormIdComponent->Id.BaseId != 0,
            hasCell, isNpcCharacter, isInRange))
        return;

    NotifyScriptAnimation message{};
    message.FormID = pFormIdComponent->Id;
    message.Animation = packet.Animation;
    message.EventName = packet.EventName;

    if (!GameServer::Get()->SendToPlayersInRange(message, source, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
