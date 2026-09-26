#include <Services/ObjectService.h>
#include <Services/DropLog.h>

#include <GameServer.h>
#include <World.h>
#include <Components.h>
#include <Services/ObjectInteractionPolicy.h>
#include <Services/PluginContainerContents.h>
#include <Services/PresentationAuthorityPolicy.h>
#include <Services/InventoryInteractionPolicy.h>
#include <Services/ContainerContentsCodec.h>

#include <Events/PlayerLeaveCellEvent.h>
#include <Events/PlayerLeaveEvent.h>
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
#include <Messages/ObjectStateReport.h>

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <cstddef>
#include <exception>

namespace
{
std::uint64_t GetUnixTimestampSeconds() noexcept
{
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return seconds > 0 ? static_cast<std::uint64_t>(seconds) : 0;
}

bool IsPlayerInObjectRange(const Player& acPlayer, const CellIdComponent& acObjectCell) noexcept
{
    const auto& playerCell = acPlayer.GetCellComponent();
    return ObjectInteractionPolicy::CanInteract(
        acObjectCell.Cell, playerCell.Cell, playerCell.WorldSpaceId, playerCell.CenterCoords,
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

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher, Persistence::WorldObjectRepository& aRepository)
    : m_world(aWorld)
    , m_repository(aRepository)
    , m_persistedStates(m_repository.LoadAll())
    , m_persistedContainers(m_repository.LoadAllContainers())
{
    RestorePersistedStates();
    spdlog::info("[World] {} persisted container(s) loaded", m_persistedContainers.size());

    m_leaveCellConnection = aDispatcher.sink<PlayerLeaveCellEvent>().connect<&ObjectService::OnPlayerLeaveCellEvent>(this);
    m_assignObjectConnection = aDispatcher.sink<PacketEvent<AssignObjectsRequest>>().connect<&ObjectService::OnAssignObjectsRequest>(this);
    m_activateConnection = aDispatcher.sink<PacketEvent<ActivateRequest>>().connect<&ObjectService::OnActivate>(this);
    m_takeWorldItemConnection = aDispatcher.sink<PacketEvent<TakeWorldItemRequest>>().connect<&ObjectService::OnTakeWorldItem>(this);
    m_lockChangeConnection = aDispatcher.sink<PacketEvent<LockChangeRequest>>().connect<&ObjectService::OnLockChange>(this);
    m_scriptAnimationConnection = aDispatcher.sink<PacketEvent<ScriptAnimationRequest>>().connect<&ObjectService::OnScriptAnimationRequest>(this);
    m_objectStateReportConnection = aDispatcher.sink<PacketEvent<ObjectStateReport>>().connect<&ObjectService::OnObjectStateReport>(this);
    m_playerLeaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&ObjectService::OnPlayerLeave>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&ObjectService::OnUpdate>(this);
}

void ObjectService::RestorePersistedStates()
{
    const auto nowUnix = GetUnixTimestampSeconds();
    auto state = m_persistedStates.begin();
    while (state != m_persistedStates.end())
    {
        bool changed = false;
        if (ObjectInteractionPolicy::IsLootRespawnDue(state->IsLootTaken, state->LootRespawnAtUnix, nowUnix))
        {
            state->IsOpenLoot = false;
            state->IsLootTaken = false;
            state->LootRespawnAtUnix = 0;
            changed = true;
        }
        if (ObjectInteractionPolicy::IsHarvestRespawnDue(state->IsHarvested, state->HarvestRespawnAtUnix, nowUnix))
        {
            state->IsHarvestable = false;
            state->IsHarvestItem = false;
            state->IsHarvested = false;
            state->HarvestRespawnAtUnix = 0;
            changed = true;
        }

        if (!ObjectInteractionPolicy::ShouldRetainWorldState(
                state->DoorStateKnown, state->ActivationCount,
                state->IsLootTaken, state->LootRespawnAtUnix,
                state->IsHarvested, state->HarvestRespawnAtUnix, nowUnix))
        {
            m_repository.EnqueueDelete(state->Id, state->CellId);
            state = m_persistedStates.erase(state);
            continue;
        }

        if (changed)
            m_repository.EnqueueUpsert(*state);
        ++state;
    }
}

void ObjectService::ApplyPersistedState(ObjectComponent& aObject, const Persistence::WorldObjectState& acState) noexcept
{
    if (acState.DoorStateKnown)
    {
        aObject.IsDoor = true;
        aObject.IsActivator = false;
        aObject.IsHarvestable = false;
        aObject.IsHarvestItem = false;
        aObject.IsOpenLoot = false;
        aObject.Door.IsKnown = true;
        aObject.Door.IsOpen = acState.DoorIsOpen;
    }

    if (acState.ActivationCount != 0)
    {
        aObject.IsDoor = false;
        aObject.IsActivator = true;
        aObject.IsHarvestable = false;
        aObject.IsHarvestItem = false;
        aObject.IsOpenLoot = false;
        aObject.Activator.ActivationCount = acState.ActivationCount;
    }

    if (acState.IsHarvested)
    {
        aObject.IsDoor = false;
        aObject.IsActivator = false;
        aObject.IsHarvestable = true;
        aObject.IsHarvestItem = acState.IsHarvestItem;
        aObject.IsOpenLoot = false;
        aObject.IsHarvested = true;
        aObject.HarvestRespawnAtUnix = acState.HarvestRespawnAtUnix;
    }

    if (acState.IsLootTaken)
    {
        aObject.IsDoor = false;
        aObject.IsActivator = false;
        aObject.IsHarvestable = false;
        aObject.IsHarvestItem = false;
        aObject.IsOpenLoot = true;
        aObject.IsLootTaken = true;
        aObject.LootRespawnAtUnix = acState.LootRespawnAtUnix;
    }
}

const Persistence::WorldObjectState* ObjectService::FindPersistedState(const GameId& acId, const GameId& acCellId) const noexcept
{
    const auto iter = std::find_if(m_persistedStates.begin(), m_persistedStates.end(), [&](const auto& acState)
    {
        return acState.Id == acId && acState.CellId == acCellId;
    });
    return iter == m_persistedStates.end() ? nullptr : &*iter;
}

const Persistence::ContainerContentsState* ObjectService::FindPersistedContainer(const GameId& acId, const GameId& acCellId) const noexcept
{
    const auto iter = std::find_if(m_persistedContainers.begin(), m_persistedContainers.end(), [&](const auto& acState)
    {
        return acState.Id == acId && acState.CellId == acCellId;
    });
    return iter == m_persistedContainers.end() ? nullptr : &*iter;
}

void ObjectService::PersistContainerContents(const entt::entity aEntity) noexcept
{
    const auto* pFormId = m_world.try_get<FormIdComponent>(aEntity);
    const auto* pObject = m_world.try_get<ObjectComponent>(aEntity);
    const auto* pCell = m_world.try_get<CellIdComponent>(aEntity);
    const auto* pInventory = m_world.try_get<InventoryComponent>(aEntity);
    if (!pFormId || !pObject || !pCell || !pInventory || !pObject->IsContainer || !pObject->HasTrustedState)
        return;

    try
    {
        auto encoded = ContainerContentsCodec::Encode(pInventory->Content);
        if (!encoded)
        {
            spdlog::error("[World] container {:X}:{:X} contents too large to persist ({} entries)", pFormId->Id.ModId, pFormId->Id.BaseId,
                pInventory->Content.Entries.size());
            return;
        }

        Persistence::ContainerContentsState state{};
        state.Id = pFormId->Id;
        state.CellId = pCell->Cell;
        state.WorldSpaceId = pCell->WorldSpaceId;
        state.CenterCoords = pCell->CenterCoords;
        state.InventoryHex = std::move(*encoded);
        m_repository.EnqueueContainerUpsert(state);

        const auto iter = std::find_if(m_persistedContainers.begin(), m_persistedContainers.end(), [&](const auto& acState)
        {
            return acState.Id == state.Id && acState.CellId == state.CellId;
        });
        if (iter == m_persistedContainers.end())
            m_persistedContainers.push_back(std::move(state));
        else
            *iter = std::move(state);
    }
    catch (const std::exception& exception)
    {
        spdlog::error("[World] could not persist container {:X}:{:X}: {}", pFormId->Id.ModId, pFormId->Id.BaseId, exception.what());
    }
}

void ObjectService::PersistState(const entt::entity aEntity) noexcept
{
    const auto* pFormId = m_world.try_get<FormIdComponent>(aEntity);
    const auto* pObject = m_world.try_get<ObjectComponent>(aEntity);
    const auto* pCell = m_world.try_get<CellIdComponent>(aEntity);
    if (!pFormId || !pObject || !pCell)
        return;

    Persistence::WorldObjectState state{};
    state.Id = pFormId->Id;
    state.CellId = pCell->Cell;
    state.WorldSpaceId = pCell->WorldSpaceId;
    state.CenterCoords = pCell->CenterCoords;
    state.IsDoor = pObject->Door.IsKnown;
    state.DoorStateKnown = pObject->Door.IsKnown;
    state.DoorIsOpen = pObject->Door.IsOpen;
    state.ActivationCount = pObject->Activator.ActivationCount;
    state.IsActivator = state.ActivationCount != 0;
    state.IsHarvestable = pObject->IsHarvested;
    state.IsHarvestItem = pObject->IsHarvested && pObject->IsHarvestItem;
    state.IsHarvested = pObject->IsHarvested;
    state.HarvestRespawnAtUnix = pObject->HarvestRespawnAtUnix;
    state.IsOpenLoot = pObject->IsLootTaken;
    state.IsLootTaken = pObject->IsLootTaken;
    state.LootRespawnAtUnix = pObject->LootRespawnAtUnix;

    const bool retain = ObjectInteractionPolicy::ShouldRetainWorldState(
        state.DoorStateKnown, state.ActivationCount, state.IsLootTaken, state.LootRespawnAtUnix,
        state.IsHarvested, state.HarvestRespawnAtUnix, GetUnixTimestampSeconds());
    if (!retain)
    {
        m_repository.EnqueueDelete(state.Id, state.CellId);
        std::erase_if(m_persistedStates, [&](const auto& acState)
        {
            return acState.Id == state.Id && acState.CellId == state.CellId;
        });
        return;
    }

    m_repository.EnqueueUpsert(state);
    const auto iter = std::find_if(m_persistedStates.begin(), m_persistedStates.end(), [&](const auto& acState)
    {
        return acState.Id == state.Id && acState.CellId == state.CellId;
    });
    if (iter == m_persistedStates.end())
    {
        try
        {
            m_persistedStates.push_back(state);
        }
        catch (const std::exception& exception)
        {
            spdlog::error("[World] could not cache persisted object {:X}:{:X}: {}", state.Id.ModId, state.Id.BaseId, exception.what());
        }
    }
    else
        *iter = state;
}

// TODO(cosideci): the cell handling of objects need to be revamped.
// We already store the location and worldspace of the mod through CellIdComponent.
// Clients need a message saying the entity was destroyed.
void ObjectService::OnPlayerLeaveCellEvent(const PlayerLeaveCellEvent&) noexcept
{
    RespawnWorldObjects();
    PruneUnobservedObjects();
}

void ObjectService::PruneUnobservedObjects() noexcept
{
    auto objectView = m_world.view<ObjectComponent, CellIdComponent>();
    Vector<entt::entity> toDestroy;
    const auto nowUnix = GetUnixTimestampSeconds();

    for (const auto entity : objectView)
    {
        const auto& objectComponent = objectView.get<ObjectComponent>(entity);
        const auto& objectCell = objectView.get<CellIdComponent>(entity);

        if (ObjectInteractionPolicy::ShouldRetainWorldState(
                objectComponent.Door.IsKnown, objectComponent.Activator.ActivationCount,
                objectComponent.IsLootTaken, objectComponent.LootRespawnAtUnix,
                objectComponent.IsHarvested, objectComponent.HarvestRespawnAtUnix, nowUnix) ||
            (objectComponent.IsContainer && objectComponent.HasTrustedState))
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
    if (acMessage.Packet.OverLimitCount != 0)
    {
        DropLog::Info("assign objects: count over limit", "player {:X}, {} object(s)", acMessage.pPlayer->GetId(), acMessage.Packet.OverLimitCount);
        return;
    }

    RespawnWorldObjects();
    auto view = m_world.view<FormIdComponent, ObjectComponent, CellIdComponent, InventoryComponent>();
    const auto& senderCell = acMessage.pPlayer->GetCellComponent();

    AssignObjectsResponse response;
    std::size_t outOfRange = 0;

    for (const ObjectData& object : acMessage.Packet.Objects)
    {
        if (!object.Id)
            continue;

        const auto iter = std::find_if(
            std::begin(view), std::end(view),
            [view, id = object.Id, cellId = object.CellId](auto entity)
            {
                const auto& formIdComponent = view.get<FormIdComponent>(entity);
                const auto& objectCell = view.get<CellIdComponent>(entity);
                return formIdComponent.Id == id && objectCell.Cell == cellId;
            });

        entt::entity entity = entt::null;
        if (iter != std::end(view))
        {
            const auto& objectCell = view.get<CellIdComponent>(*iter);
            if (!ObjectInteractionPolicy::CanInteract(
                    object.CellId, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
                    objectCell.Cell, objectCell.WorldSpaceId, objectCell.CenterCoords))
            {
                ++outOfRange;
                continue;
            }

            entity = *iter;
        }
        else
        {
            const auto* pPersistedState = FindPersistedState(object.Id, object.CellId);
            const GameId& cellId = pPersistedState ? pPersistedState->CellId : object.CellId;
            const GameId& worldSpaceId = pPersistedState ? pPersistedState->WorldSpaceId : object.WorldSpaceId;
            const GridCellCoords& centerCoords = pPersistedState ? pPersistedState->CenterCoords : object.CurrentCoords;
            if (!ObjectInteractionPolicy::CanDiscover(
                    object.Id, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
                    cellId, worldSpaceId, centerCoords))
            {
                ++outOfRange;
                continue;
            }

            entity = m_world.create();

            m_world.emplace<FormIdComponent>(entity, object.Id);

            auto& objectComponent = m_world.emplace<ObjectComponent>(entity, acMessage.pPlayer);
            objectComponent.IsHarvestable = object.IsHarvestable;
            objectComponent.IsHarvestItem = object.IsHarvestable && object.IsHarvestItem;
            objectComponent.IsOpenLoot = object.IsOpenLoot && !object.IsHarvestable;
            objectComponent.IsFurniture = object.IsFurniture;
            objectComponent.IsDoor = object.IsDoor;
            objectComponent.IsActivator = object.IsActivator && !object.IsDoor && !object.IsHarvestable;

            m_world.emplace<CellIdComponent>(entity, cellId, worldSpaceId, centerCoords);
            auto& inventoryComponent = m_world.emplace<InventoryComponent>(entity);

            // Container baseline, in priority order: contents persisted by an earlier transfer;
            // contents the server builds from the CONT record at the place's fixed level
            // (world-state plan, phase 1b); otherwise the first discoverer's contents, logged.
            if (object.IsContainer && !object.IsDoor && !object.IsHarvestable && !objectComponent.IsActivator)
            {
                std::optional<Inventory> persistedContents;
                if (const auto* pPersistedContainer = FindPersistedContainer(object.Id, object.CellId))
                {
                    persistedContents = ContainerContentsCodec::Decode(pPersistedContainer->InventoryHex);
                    if (!persistedContents)
                        spdlog::warn("[World] container {:X}:{:X} persisted contents are corrupt; falling back to the plugin or client baseline",
                            object.Id.ModId, object.Id.BaseId);
                }

                std::optional<PluginContainerContents::Result> pluginContents;
                if (!persistedContents)
                    pluginContents = m_world.ctx().at<PluginContainerContents>().Build(object.Id, m_world.ctx().at<ModsComponent>());

                if (pluginContents)
                {
                    inventoryComponent.Content = std::move(pluginContents->Contents);
                    spdlog::info(
                        "[World] container {:X}:{:X} contents from plugins: {} entries at level {} (zone {:X}); client reported {}", object.Id.ModId,
                        object.Id.BaseId, inventoryComponent.Content.Entries.size(), pluginContents->Level, pluginContents->ZoneId,
                        object.CurrentInventory.Entries.size());
                }
                else
                {
                    const auto& source = persistedContents ? persistedContents->Entries : object.CurrentInventory.Entries;
                    for (const auto& entry : source)
                    {
                        if (entry.Count > 0 && InventoryInteractionPolicy::HasValidItemPayload(entry))
                            inventoryComponent.Content.AddOrRemoveEntry(entry);
                    }
                    if (persistedContents)
                        spdlog::info("[World] container {:X}:{:X} restored from persistence: {} entries (client reported {})", object.Id.ModId,
                            object.Id.BaseId, inventoryComponent.Content.Entries.size(), object.CurrentInventory.Entries.size());
                    else
                        spdlog::info(
                            "[World] container {:X}:{:X} baseline learned from player {:X}: {} entries ({} reported)", object.Id.ModId, object.Id.BaseId,
                            acMessage.pPlayer->GetId(), inventoryComponent.Content.Entries.size(), object.CurrentInventory.Entries.size());
                }
                objectComponent.IsContainer = true;
                objectComponent.HasTrustedState = true;
                // Trusted lock data is sent to every client, so it has to start as the plugin placed it;
                // a default "unlocked" opened every locked chest for the next player.
                if (const auto lock = m_world.ctx().at<PluginContainerContents>().InitialLock(object.Id, m_world.ctx().at<ModsComponent>()))
                    objectComponent.CurrentLockData = *lock;
            }
        }

        auto& objectComponent = view.get<ObjectComponent>(entity);
        if (const auto* pPersistedState = FindPersistedState(object.Id, object.CellId))
            ApplyPersistedState(objectComponent, *pPersistedState);

        ObjectData objectData;
        objectData.Id = view.get<FormIdComponent>(entity).Id;
        objectData.ServerId = World::ToInteger(entity);
        objectData.IsStateUntrusted = !objectComponent.HasTrustedState;
        objectData.IsHarvestable = objectComponent.IsHarvestable;
        objectData.IsHarvestItem = objectComponent.IsHarvestItem;
        objectData.IsHarvested = objectComponent.IsHarvested;
        objectData.IsOpenLoot = objectComponent.IsOpenLoot;
        objectData.IsLootTaken = objectComponent.IsLootTaken;
        objectData.IsFurniture = objectComponent.IsFurniture;
        objectData.IsDoor = objectComponent.IsDoor;
        objectData.IsDoorStateKnown = objectComponent.Door.IsKnown;
        objectData.IsDoorOpen = objectComponent.Door.IsOpen;
        objectData.IsActivator = objectComponent.IsActivator;
        objectData.ActivationCount = objectComponent.Activator.ActivationCount;
        objectData.IsContainer = objectComponent.IsContainer;
        if (objectComponent.HasTrustedState)
        {
            objectData.CurrentLockData = objectComponent.CurrentLockData;
            objectData.CurrentInventory = view.get<InventoryComponent>(entity).Content;
        }

        response.Objects.push_back(objectData);
    }

    // Every skip here is silent to the client, so a stale sender cell once looked like "nothing syncs".
    if (outOfRange > 0)
        spdlog::info("[World] assign objects from player {:X}: {} of {} object(s) out of range (sender cell {:X}:{:X}, first object cell {:X}:{:X})",
            acMessage.pPlayer->GetId(), outOfRange, acMessage.Packet.Objects.size(), senderCell.Cell.ModId, senderCell.Cell.BaseId,
            acMessage.Packet.Objects.empty() ? 0u : acMessage.Packet.Objects[0].CellId.ModId, acMessage.Packet.Objects.empty() ? 0u : acMessage.Packet.Objects[0].CellId.BaseId);

    if (!response.Objects.empty())
        acMessage.pPlayer->Send(response);
}

void ObjectService::OnTakeWorldItem(const PacketEvent<TakeWorldItemRequest>& acMessage) noexcept
{
    RespawnWorldObjects();
    const auto& packet = acMessage.Packet;
    const auto objectView = m_world.view<FormIdComponent, ObjectComponent, CellIdComponent>();
    const auto objectIt = std::find_if(
        objectView.begin(), objectView.end(),
        [objectView, id = packet.Id, cellId = packet.CellId](const auto entity)
        {
            return objectView.get<FormIdComponent>(entity).Id == id && objectView.get<CellIdComponent>(entity).Cell == cellId;
        });
    if (objectIt == objectView.end())
    {
        DropLog::Info("take world item: object not registered", "player {:X}, object {:X}:{:X}, cell {:X}:{:X}", acMessage.pPlayer->GetId(),
            packet.Id.ModId, packet.Id.BaseId, packet.CellId.ModId, packet.CellId.BaseId);
        return;
    }

    const auto& senderCell = acMessage.pPlayer->GetCellComponent();
    const auto& objectCell = objectView.get<CellIdComponent>(*objectIt);
    auto& objectComponent = objectView.get<ObjectComponent>(*objectIt);

    const auto activatorEntity = static_cast<entt::entity>(packet.ActivatorId);
    const auto activatorView = m_world.view<CharacterComponent, OwnerComponent, CellIdComponent>();
    const auto activatorIt = activatorView.find(activatorEntity);
    const bool activatorExists = activatorIt != activatorView.end();
    const bool ownedBySender = activatorExists && activatorView.get<OwnerComponent>(*activatorIt).GetOwner() == acMessage.pPlayer;
    if (!activatorExists)
    {
        DropLog::Info("take world item: actor not found", "player {:X}, actor {:X}, object {:X}:{:X}", acMessage.pPlayer->GetId(),
            packet.ActivatorId, packet.Id.ModId, packet.Id.BaseId);
        return;
    }

    const auto& activatorCell = activatorView.get<CellIdComponent>(*activatorIt);
    std::size_t notifiedPlayers = 0;
    const bool taken = ObjectInteractionPolicy::TryTakeWorldItem(
        true, // The discovered entity is registered server-side; actor ownership, cell, and range are checked below.
        objectComponent.IsOpenLoot && !objectComponent.IsHarvestable,
        objectComponent.IsLootTaken, activatorExists, ownedBySender,
        packet.CellId, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
        activatorCell.Cell, activatorCell.WorldSpaceId, activatorCell.CenterCoords,
        objectCell.Cell, objectCell.WorldSpaceId, objectCell.CenterCoords,
        [&]
        {
            NotifyWorldItemTaken notify{};
            notify.Id = packet.Id;
            notify.IsTaken = true;
            notifiedPlayers = NotifyObjectPeersInRange(m_world, notify, objectCell, acMessage.pPlayer);
        });

    if (taken)
    {
        objectComponent.LootRespawnAtUnix = ObjectInteractionPolicy::ItemRespawnAtUnix(GetUnixTimestampSeconds());
        PersistState(*objectIt);
        spdlog::info("[World] loot {:X}:{:X} taken; notified {} peer(s)", packet.Id.ModId, packet.Id.BaseId, notifiedPlayers);
    }
    else
        spdlog::debug("World loot pickup {:X}:{:X} rejected (already taken or not authorized/in range)", packet.Id.ModId, packet.Id.BaseId);
}

void ObjectService::OnActivate(const PacketEvent<ActivateRequest>& acMessage) noexcept
{
    const auto& packet = acMessage.Packet;
    if (!ObjectInteractionPolicy::IsValidOpenState(packet.PreActivationOpenState))
    {
        DropLog::Info("activate: invalid open state", "player {:X}, object {:X}:{:X}, state {}", acMessage.pPlayer->GetId(), packet.Id.ModId,
            packet.Id.BaseId, packet.PreActivationOpenState);
        return;
    }

    RespawnWorldObjects();

    const auto objectView = m_world.view<FormIdComponent, ObjectComponent, CellIdComponent>();
    const auto objectIt = std::find_if(
        objectView.begin(), objectView.end(),
        [objectView, id = packet.Id, cellId = packet.CellId](const auto entity)
        {
            return objectView.get<FormIdComponent>(entity).Id == id && objectView.get<CellIdComponent>(entity).Cell == cellId;
        });
    if (objectIt == objectView.end())
    {
        DropLog::Info("activate: object not registered", "player {:X}, object {:X}:{:X}, cell {:X}:{:X}", acMessage.pPlayer->GetId(),
            packet.Id.ModId, packet.Id.BaseId, packet.CellId.ModId, packet.CellId.BaseId);
        return;
    }

    const auto& senderCell = acMessage.pPlayer->GetCellComponent();
    const auto& objectCell = objectView.get<CellIdComponent>(*objectIt);
    auto& objectComponent = objectView.get<ObjectComponent>(*objectIt);

    const auto activatorEntity = static_cast<entt::entity>(packet.ActivatorId);
    const auto activatorView = m_world.view<CharacterComponent, OwnerComponent, CellIdComponent>();
    const auto activatorIt = activatorView.find(activatorEntity);
    const bool activatorExists = activatorIt != activatorView.end();
    const bool ownedBySender = activatorExists && activatorView.get<OwnerComponent>(*activatorIt).GetOwner() == acMessage.pPlayer;
    if (!activatorExists)
    {
        DropLog::Info("activate: actor not found", "player {:X}, actor {:X}, object {:X}:{:X}", acMessage.pPlayer->GetId(), packet.ActivatorId,
            packet.Id.ModId, packet.Id.BaseId);
        return;
    }

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
            objectComponent.HarvestRespawnAtUnix = ObjectInteractionPolicy::HarvestRespawnAtUnix(GetUnixTimestampSeconds(), objectComponent.IsHarvestItem);
            PersistState(*objectIt);
            spdlog::info("[World] harvest {:X}:{:X} accepted; notified {} peer(s)", packet.Id.ModId, packet.Id.BaseId, notifiedPlayers);
        }
        else
            spdlog::info("Harvest of {:X}:{:X} rejected (already harvested or out of range)", packet.Id.ModId, packet.Id.BaseId);
        return;
    }

    if (objectComponent.IsDoor)
    {
        std::size_t recipientCount = 0;
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
                recipientCount = NotifyObjectPeersInRange(m_world, notifyActivate, objectCell, acMessage.pPlayer);
            });
        if (toggled)
        {
            PersistState(*objectIt);
            spdlog::info("[World] door {:X}:{:X} is now {} (player {:X}, cell {:X}:{:X}, peers {})", packet.Id.ModId, packet.Id.BaseId, objectComponent.Door.IsOpen ? "open" : "closed", acMessage.pPlayer->GetId(), objectCell.Cell.ModId, objectCell.Cell.BaseId, recipientCount);
        }
        else
            spdlog::info("[World] door {:X}:{:X} toggle rejected (player {:X}, pre-state {}, requested cell {:X}:{:X}, object cell {:X}:{:X})", packet.Id.ModId, packet.Id.BaseId, acMessage.pPlayer->GetId(), packet.PreActivationOpenState, packet.CellId.ModId, packet.CellId.BaseId, objectCell.Cell.ModId, objectCell.Cell.BaseId);
        return;
    }

    if (objectComponent.IsActivator)
    {
        std::size_t recipientCount = 0;
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
                recipientCount = NotifyObjectPeersInRange(m_world, notifyActivate, objectCell, acMessage.pPlayer);
            });
        if (relayed)
        {
            PersistState(*objectIt);
            spdlog::info("[World] activator {:X}:{:X} activation #{} from player {:X}, cell {:X}:{:X}, peers {}", packet.Id.ModId, packet.Id.BaseId, objectComponent.Activator.ActivationCount, acMessage.pPlayer->GetId(), objectCell.Cell.ModId, objectCell.Cell.BaseId, recipientCount);
        }
        else
            spdlog::info("[World] activator {:X}:{:X} activation rejected (player {:X}, requested cell {:X}:{:X}, object cell {:X}:{:X}, cooldown or range/ownership)", packet.Id.ModId, packet.Id.BaseId, acMessage.pPlayer->GetId(), packet.CellId.ModId, packet.CellId.BaseId, objectCell.Cell.ModId, objectCell.Cell.BaseId);
        return;
    }

    if (!ObjectInteractionPolicy::CanActivate(
            objectComponent.HasTrustedState, activatorExists, ownedBySender,
            packet.CellId, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
            activatorCell.Cell, activatorCell.WorldSpaceId, activatorCell.CenterCoords,
            objectCell.Cell, objectCell.WorldSpaceId, objectCell.CenterCoords))
    {
        DropLog::Info("activate: not allowed", "player {:X}, object {:X}:{:X}, trusted {}, owns actor {}, requested cell {:X}:{:X}, object cell {:X}:{:X}",
            acMessage.pPlayer->GetId(), packet.Id.ModId, packet.Id.BaseId, objectComponent.HasTrustedState, ownedBySender, packet.CellId.ModId,
            packet.CellId.BaseId, objectCell.Cell.ModId, objectCell.Cell.BaseId);
        return;
    }

    NotifyActivate notifyActivate;
    notifyActivate.Id = packet.Id;
    notifyActivate.ActivatorId = packet.ActivatorId;
    notifyActivate.PreActivationOpenState = packet.PreActivationOpenState;

    NotifyObjectPeersInRange(m_world, notifyActivate, objectCell, acMessage.pPlayer);
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
        RespawnWorldObjects();
        PruneUnobservedObjects();
    }
}

void ObjectService::RespawnWorldObjects() noexcept
{
    auto view = m_world.view<FormIdComponent, ObjectComponent, CellIdComponent>();
    const auto nowUnix = GetUnixTimestampSeconds();
    for (auto entity : view)
    {
        auto& objectComponent = view.get<ObjectComponent>(entity);

        if (ObjectInteractionPolicy::IsLootRespawnDue(objectComponent.IsLootTaken, objectComponent.LootRespawnAtUnix, nowUnix))
        {
            objectComponent.IsLootTaken = false;
            objectComponent.LootRespawnAtUnix = 0;
            PersistState(entity);

            NotifyWorldItemTaken notifyRespawned{};
            notifyRespawned.Id = view.get<FormIdComponent>(entity).Id;
            notifyRespawned.IsTaken = false;
            const auto& objectCell = view.get<CellIdComponent>(entity);
            const std::size_t notifiedPlayers = NotifyObjectPeersInRange(m_world, notifyRespawned, objectCell, nullptr);
            spdlog::info("[World] loot respawn {:X}:{:X} notified={}", notifyRespawned.Id.ModId, notifyRespawned.Id.BaseId, notifiedPlayers);
        }

        if (!ObjectInteractionPolicy::IsHarvestRespawnDue(objectComponent.IsHarvested, objectComponent.HarvestRespawnAtUnix, nowUnix))
            continue;

        objectComponent.IsHarvested = false;
        objectComponent.HarvestRespawnAtUnix = 0;
        PersistState(entity);

        NotifyObjectHarvested notifyRespawned;
        notifyRespawned.Id = view.get<FormIdComponent>(entity).Id;
        notifyRespawned.IsHarvested = false;

        const auto& objectCell = view.get<CellIdComponent>(entity);
        const std::size_t notifiedPlayers = NotifyObjectPeersInRange(m_world, notifyRespawned, objectCell, nullptr);

        spdlog::info("[World] harvest respawn {:X}:{:X} at={} notified={}", notifyRespawned.Id.ModId, notifyRespawned.Id.BaseId, nowUnix, notifiedPlayers);
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
    {
        DropLog::Info("lock change: object not registered", "player {:X}, object {:X}:{:X}, cell {:X}:{:X}", acMessage.pPlayer->GetId(),
            packet.Id.ModId, packet.Id.BaseId, packet.CellId.ModId, packet.CellId.BaseId);
        return;
    }

    const auto& senderCell = acMessage.pPlayer->GetCellComponent();
    const auto& objectCell = objectView.get<CellIdComponent>(*iter);
    if (!ObjectInteractionPolicy::CanInteract(
            packet.CellId, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
            objectCell.Cell, objectCell.WorldSpaceId, objectCell.CenterCoords))
    {
        DropLog::Info("lock change: out of range", "player {:X}, object {:X}:{:X}, requested cell {:X}:{:X}, sender cell {:X}:{:X}, object cell {:X}:{:X}",
            acMessage.pPlayer->GetId(), packet.Id.ModId, packet.Id.BaseId, packet.CellId.ModId, packet.CellId.BaseId, senderCell.Cell.ModId,
            senderCell.Cell.BaseId, objectCell.Cell.ModId, objectCell.Cell.BaseId);
        return;
    }

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
    {
        DropLog::Info("lock change: rejected", "player {:X}, object {:X}:{:X}, trusted {}, locked {}, level {}", acMessage.pPlayer->GetId(),
            packet.Id.ModId, packet.Id.BaseId, objectComponent.HasTrustedState, packet.IsLocked, packet.LockLevel);
        return;
    }
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

void ObjectService::OnObjectStateReport(const PacketEvent<ObjectStateReport>& acMessage) noexcept
{
    const auto& packet = acMessage.Packet;
    const uint32_t cPlayerId = acMessage.pPlayer->GetId();
    if (packet.IsMalformed)
    {
        DropLog::Info("object state report: malformed", "player {:X}", cPlayerId);
        return;
    }

    auto view = m_world.view<FormIdComponent, ObjectComponent, CellIdComponent, InventoryComponent>();
    // A report carries every synced reference of the loaded cells; index once instead of a view scan per digest.
    std::unordered_multimap<GameId, entt::entity> entitiesById;
    for (auto entity : view)
        entitiesById.emplace(view.get<FormIdComponent>(entity).Id, entity);

    std::size_t mismatched = 0;

    for (const ObjectStateDigest& digest : packet.Objects)
    {
        entt::entity entity = entt::null;
        const auto [first, last] = entitiesById.equal_range(digest.Id);
        for (auto it = first; it != last; ++it)
        {
            if (view.get<CellIdComponent>(it->second).Cell == digest.CellId)
            {
                entity = it->second;
                break;
            }
        }

        // Not registered yet (or pruned): nothing server-owned to compare.
        if (entity == entt::null)
            continue;

        const auto& object = view.get<ObjectComponent>(entity);
        DesyncPolicy::ServerView server{};
        server.HasTrustedState = object.HasTrustedState;
        server.IsHarvestable = object.IsHarvestable;
        server.IsHarvested = object.IsHarvested;
        server.IsOpenLoot = object.IsOpenLoot;
        server.IsLootTaken = object.IsLootTaken;
        server.IsDoor = object.IsDoor;
        server.IsDoorStateKnown = object.Door.IsKnown;
        server.IsDoorOpen = object.Door.IsOpen;
        server.IsContainer = object.IsContainer;
        server.IsLocked = object.CurrentLockData.IsLocked;
        server.LockLevel = object.CurrentLockData.LockLevel;
        if (object.IsContainer)
            server.Items = ObjectStateDigest::Canonicalize(view.get<InventoryComponent>(entity).Content);

        const auto mismatches = DesyncPolicy::Compare(server, digest);
        for (const auto field : {DesyncPolicy::Field::kHarvested, DesyncPolicy::Field::kLootTaken, DesyncPolicy::Field::kLock, DesyncPolicy::Field::kDoor, DesyncPolicy::Field::kInventory})
        {
            const auto found = std::find_if(mismatches.begin(), mismatches.end(), [field](const auto& acMismatch) { return acMismatch.Kind == field; });
            const std::string signature = found == mismatches.end() ? std::string{} : found->Server + " | " + found->Client;
            const auto event = m_desyncTracker.Observe({cPlayerId, digest.Id, field}, signature);
            if (event == DesyncPolicy::Tracker::Event::kNew)
                spdlog::warn(
                    "[Desync] player {:X} ref {:X}:{:X} cell {:X}:{:X} {}: server {} client {}", cPlayerId, digest.Id.ModId, digest.Id.BaseId, digest.CellId.ModId,
                    digest.CellId.BaseId, DesyncPolicy::FieldName(field), found->Server, found->Client);
            else if (event == DesyncPolicy::Tracker::Event::kResolved)
                spdlog::info("[Desync] player {:X} ref {:X}:{:X} {}: resolved", cPlayerId, digest.Id.ModId, digest.Id.BaseId, DesyncPolicy::FieldName(field));
        }
        mismatched += mismatches.empty() ? 0 : 1;
    }

    spdlog::debug("[Desync] report from player {:X}: {} object(s), {} differing", cPlayerId, packet.Objects.size(), mismatched);
}

void ObjectService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_desyncTracker.ForgetPlayer(acEvent.pPlayer->GetId());
}
