#include <Services/ObjectService.h>

#include <GameServer.h>
#include <World.h>
#include <Components.h>
#include <Services/ObjectInteractionPolicy.h>

#include <Events/PlayerLeaveCellEvent.h>

#include <Messages/ActivateRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/NotifyLockChange.h>
#include <Messages/AssignObjectsRequest.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Messages/NotifyScriptAnimation.h>

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_leaveCellConnection = aDispatcher.sink<PlayerLeaveCellEvent>().connect<&ObjectService::OnPlayerLeaveCellEvent>(this);
    m_assignObjectConnection = aDispatcher.sink<PacketEvent<AssignObjectsRequest>>().connect<&ObjectService::OnAssignObjectsRequest>(this);
    m_activateConnection = aDispatcher.sink<PacketEvent<ActivateRequest>>().connect<&ObjectService::OnActivate>(this);
    m_lockChangeConnection = aDispatcher.sink<PacketEvent<LockChangeRequest>>().connect<&ObjectService::OnLockChange>(this);
    m_scriptAnimationConnection = aDispatcher.sink<PacketEvent<ScriptAnimationRequest>>().connect<&ObjectService::OnScriptAnimationRequest>(this);
}

// TODO(cosideci): the cell handling of objects need to be revamped.
// We already store the location and worldspace of the mod through CellIdComponent.
// Clients need a message saying the entity was destroyed.
void ObjectService::OnPlayerLeaveCellEvent(const PlayerLeaveCellEvent& acEvent) noexcept
{
    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer->GetCellComponent().Cell == acEvent.OldCell)
            return;
    }

    auto objectView = m_world.view<ObjectComponent, CellIdComponent>();
    Vector<entt::entity> toDestroy;

    for (auto entity : objectView)
    {
        const auto& cellIdComponent = objectView.get<CellIdComponent>(entity);

        if (cellIdComponent.Cell != acEvent.OldCell)
            continue;

        toDestroy.push_back(entity);
    }

    for (auto& entity : toDestroy)
    {
        m_world.destroy(entity);
    }
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

            m_world.emplace<ObjectComponent>(cEntity, acMessage.pPlayer);

            m_world.emplace<CellIdComponent>(cEntity, object.CellId, object.WorldSpaceId, object.CurrentCoords);
            m_world.emplace<InventoryComponent>(cEntity);

            ObjectData objectData;
            objectData.Id = object.Id;
            objectData.ServerId = World::ToInteger(cEntity);
            objectData.IsStateUntrusted = true;

            response.Objects.push_back(objectData);
        }
    }

    if (!response.Objects.empty())
        acMessage.pPlayer->Send(response);
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

    const auto activatorEntity = static_cast<entt::entity>(packet.ActivatorId);
    const auto activatorView = m_world.view<CharacterComponent, OwnerComponent, CellIdComponent>();
    const auto activatorIt = activatorView.find(activatorEntity);
    const bool activatorExists = activatorIt != activatorView.end();
    const bool ownedBySender = activatorExists && activatorView.get<OwnerComponent>(*activatorIt).GetOwner() == acMessage.pPlayer;
    if (!activatorExists)
        return;

    const auto& activatorCell = activatorView.get<CellIdComponent>(*activatorIt);
    if (!ObjectInteractionPolicy::CanActivate(
            activatorExists, ownedBySender,
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
    if (!ObjectInteractionPolicy::TryApplyLockChange(
            objectComponent.HasTrustedState, objectComponent.CurrentLockData, packet.IsLocked, packet.LockLevel))
        return;

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
}

void ObjectService::OnScriptAnimationRequest(const PacketEvent<ScriptAnimationRequest>& acMessage) noexcept
{
    auto& packet = acMessage.Packet;

    NotifyScriptAnimation message{};
    message.FormID = packet.FormID;
    message.Animation = packet.Animation;
    message.EventName = packet.EventName;

    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        pPlayer->Send(message);
    }
}
