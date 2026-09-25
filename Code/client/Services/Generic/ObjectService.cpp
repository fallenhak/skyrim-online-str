#include <Services/ObjectService.h>
#include <Games/ActorExtension.h>
#include <Services/LocalOnlyActivators.h>
#include <Services/ObjectSyncPolicy.h>
#include <Services/ActivatorReplayPolicy.h>
#include <Services/WorldObjectTrackingPolicy.h>

#include <World.h>
#include <Utils.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/ActivateEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/ScriptAnimationEvent.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/AssignObjectsRequest.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/ActivateRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/NotifyLockChange.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Messages/NotifyScriptAnimation.h>
#include <Messages/NotifyObjectHarvested.h>
#include <Messages/TakeWorldItemRequest.h>
#include <Messages/NotifyWorldItemTaken.h>

#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/BGSEncounterZone.h>
#include <Forms/TESFaction.h>
#include <Games/TES.h>

#include <inttypes.h>

#include <limits>
#include <unordered_map>

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&ObjectService::OnDisconnected>(this);
    m_cellChangeConnection = aDispatcher.sink<CellChangeEvent>().connect<&ObjectService::OnCellChange>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&ObjectService::OnUpdate>(this);
    m_onActivateConnection = aDispatcher.sink<ActivateEvent>().connect<&ObjectService::OnActivate>(this);
    m_activateConnection = aDispatcher.sink<NotifyActivate>().connect<&ObjectService::OnActivateNotify>(this);
    m_lockChangeConnection = aDispatcher.sink<LockChangeEvent>().connect<&ObjectService::OnLockChange>(this);
    m_lockChangeNotifyConnection = aDispatcher.sink<NotifyLockChange>().connect<&ObjectService::OnLockChangeNotify>(this);
    m_assignObjectConnection = aDispatcher.sink<AssignObjectsResponse>().connect<&ObjectService::OnAssignObjectsResponse>(this);
    m_scriptAnimationConnection = aDispatcher.sink<ScriptAnimationEvent>().connect<&ObjectService::OnScriptAnimationEvent>(this);
    m_scriptAnimationNotifyConnection = aDispatcher.sink<NotifyScriptAnimation>().connect<&ObjectService::OnNotifyScriptAnimation>(this);
    m_objectHarvestedConnection = aDispatcher.sink<NotifyObjectHarvested>().connect<&ObjectService::OnObjectHarvestedNotify>(this);
    m_worldItemTakenConnection = aDispatcher.sink<NotifyWorldItemTaken>().connect<&ObjectService::OnWorldItemTakenNotify>(this);

    EventDispatcherManager::Get()->activateEvent.RegisterSink(this);
}

bool IsPlayerHome(const TESObjectCELL* pCell) noexcept
{
    if (pCell && pCell->loadedCellData && pCell->loadedCellData->encounterZone)
    {
        // Only return true if cell has the NoResetZone encounter zone
        if (pCell->loadedCellData->encounterZone->formID == 0xf90b1)
        {
            switch (pCell->formID)
            {
            case 0xeec55: // one known exception: Sinderion's Field Lab
                return false;
            default: return true;
            }
        }
    }

    return false;
}

// Find each loaded faction's containers for a jailed player's belongings and stolen items.
// Do not sync them: each player's items must stay separate (#700).
// Only compare the container pointers; never read through them.
Set<const TESObjectREFR*> GetPlayerStashContainers() noexcept
{
    Set<const TESObjectREFR*> containers{};

    ModManager* pModManager = ModManager::Get();
    if (!pModManager)
        return containers;

    for (const TESFaction* pFaction : pModManager->factions)
    {
        if (!pFaction)
            continue;

        if (pFaction->crimeData.playerInventoryContainer)
            containers.insert(pFaction->crimeData.playerInventoryContainer);

        if (pFaction->crimeData.stolenGoodsContainer)
            containers.insert(pFaction->crimeData.stolenGoodsContainer);
    }

    return containers;
}

bool ShouldSyncObject(const TESObjectREFR* apObject, const Set<const TESObjectREFR*>& acPlayerStashContainers) noexcept
{
    if (!apObject)
        return false;

    if (acPlayerStashContainers.contains(apObject))
        return false;

    // Quest chests that take the player's whole inventory without going through faction crime data.
    switch (apObject->formID)
    {
    case 0x39CF1: // Don't sync the chest in the "Diplomatic Immunity" quest
        return false;
    case 0x3EF03: // ...as well as in the "No One Escapes Cidhna Mine" quest
        return false;
    default:
        return true;
    }
}

// Flora and ingredients placed in the plugin (eggs, mushrooms). Dropped items are
// temporaries with machine-local ids and cannot be matched across clients.
bool IsHarvestableObject(const TESObjectREFR* apObject) noexcept
{
    if (!apObject || !apObject->baseForm || apObject->IsTemporary())
        return false;

    const FormType cType = apObject->baseForm->formType;
    return cType == FormType::Flora || cType == FormType::Ingredient;
}

// Refs this service disabled, so a server reset re-enables only those and
// never a ref a quest or script disabled.
Set<uint32_t> s_harvestDisabledRefs{};
Set<uint32_t> s_worldItemDisabledRefs{};
std::unordered_map<uint32_t, uint32_t> s_appliedActivatorActivationCounts{};
// Last lock state sent per object. The game can re-raise the same lock state many
// times in a row (e.g. on cell load); only real changes are worth a request.
std::unordered_map<uint32_t, std::pair<bool, uint8_t>> s_sentLockStates{};
uint32_t s_replayingActivatorFormId{};

void IncrementAppliedActivatorCount(const uint32_t aFormId) noexcept
{
    auto& count = s_appliedActivatorActivationCounts[aFormId];
    if (count != std::numeric_limits<uint32_t>::max())
        ++count;
}

struct ScopedActivatorStateReplay final
{
    explicit ScopedActivatorStateReplay(const uint32_t aFormId) noexcept
        : PreviousFormId(s_replayingActivatorFormId)
    {
        s_replayingActivatorFormId = aFormId;
    }

    ~ScopedActivatorStateReplay() noexcept { s_replayingActivatorFormId = PreviousFormId; }

    uint32_t PreviousFormId{};
};

void TrackLocalWorldItemTaken(const uint32_t aFormId) noexcept
{
    s_worldItemDisabledRefs.insert(aFormId);
}

// Another player took it: hide it here too. Only the harvester receives the item.
void ApplyHarvested(TESObjectREFR* apObject) noexcept
{
    if (!IsHarvestableObject(apObject))
        return;

    // A local activation is recorded in OnActivate before we reach here. Leave other
    // disabled references untracked so quest/script state is never re-enabled by respawn.
    if (apObject->IsDisabled())
        return;

    spdlog::info("Object {:X} harvested remotely, disabling", apObject->formID);
    apObject->Disable();
    s_harvestDisabledRefs.insert(apObject->formID);
}

// The server reports that the harvest timer has expired.
void RestoreHarvested(TESObjectREFR* apObject) noexcept
{
    if (!apObject || !s_harvestDisabledRefs.erase(apObject->formID))
        return;

    if (apObject->IsDisabled())
    {
        spdlog::info("Object {:X} no longer harvested, enabling", apObject->formID);
        apObject->Enable();
    }
}

void ApplyWorldItemTaken(TESObjectREFR* apObject) noexcept
{
    if (!ObjectSyncPolicy::IsOpenLootObject(apObject) || apObject->IsDisabled())
        return;

    spdlog::info("World item {:X} taken remotely, disabling", apObject->formID);
    // No fade: the item is already in someone else's hands.
    apObject->Disable(false);
    s_worldItemDisabledRefs.insert(apObject->formID);
}

void RestoreWorldItem(TESObjectREFR* apObject) noexcept
{
    if (!apObject || !s_worldItemDisabledRefs.erase(apObject->formID))
        return;

    if (apObject->IsDisabled())
    {
        spdlog::info("World item {:X} respawned, enabling", apObject->formID);
        apObject->Enable();
    }
}

// Load doors teleport the activator, so they are never toggled remotely.
bool IsSyncedDoor(TESObjectREFR* apObject) noexcept
{
    if (!apObject || !apObject->baseForm || apObject->baseForm->formType != FormType::Door)
        return false;

    ExtraDataList* pExtraData = apObject->GetExtraDataList();
    return !pExtraData || !pExtraData->Contains(ExtraDataType::Teleport);
}

// Plugin-placed activators (levers, chains, buttons, puzzle pillars). Their
// script runs on every client when the server relays the activation. Ore veins,
// shrines, crafting triggers and critters stay local (LocalOnlyActivators).
bool IsSyncedActivator(const TESObjectREFR* apObject) noexcept
{
    return apObject && apObject->baseForm && !apObject->IsTemporary() && apObject->baseForm->formType == FormType::Activator &&
        !LocalOnlyActivators::Contains(apObject->baseForm->formID);
}

void ObjectService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_assignObjectsPending = false;
    s_sentLockStates.clear();
    // TODO(cosideci): clear object components
}

void ObjectService::OnCellChange(const CellChangeEvent&) noexcept
{
    if (m_transport.IsConnected())
        m_assignObjectsPending = true;
}

void ObjectService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_assignObjectsPending)
        return;

    m_assignObjectsPending = false;
    SendAssignObjectsRequest();
}

void ObjectService::SendAssignObjectsRequest() noexcept
{
    if (!m_transport.IsConnected())
        return;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = pPlayer->parentCell;
    if (!pCell)
        return;

    GameId worldSpaceId{};
    if (TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace())
    {
        if (!m_world.GetModSystem().GetServerModId(pWorldSpace->formID, worldSpaceId))
        {
            spdlog::error("Server world space id not found for world space form id {:X}", pWorldSpace->formID);
            return;
        }
    }

    // In exteriors the player can reach objects in the neighbouring loaded cells,
    // and activations report the object's own cell. Register every loaded grid cell,
    // each object under its own cell, or those activations are dropped as unregistered.
    Vector<TESObjectCELL*> cells;
    const TES* pTES = TES::Get();
    if (pTES && !pTES->interiorCell && pTES->cells && pTES->cells->arr)
    {
        const uint32_t cDimension = pTES->cells->dimension;
        for (uint32_t i = 0; i < cDimension * cDimension; ++i)
        {
            TESObjectCELL* pGridCell = pTES->cells->arr[i];
            if (pGridCell && std::find(cells.begin(), cells.end(), pGridCell) == cells.end())
                cells.push_back(pGridCell);
        }
    }
    if (std::find(cells.begin(), cells.end(), pCell) == cells.end())
        cells.push_back(pCell);

    Vector<FormType> formTypes = {FormType::Container, FormType::Door, FormType::Flora, FormType::Ingredient, FormType::Furniture, FormType::Activator,
                                  FormType::Armor, FormType::Misc, FormType::Weapon, FormType::Ammo, FormType::Key,
                                  FormType::Alchemy, FormType::Scroll, FormType::SoulGem, FormType::Light, FormType::Apparatus, FormType::Book};
    // Door seemed to be at the wrong form id (29, now 32), verify this.

    AssignObjectsRequest request{};

    const Set<const TESObjectREFR*> playerStashContainers = GetPlayerStashContainers();

    for (TESObjectCELL* pObjectCell : cells)
    {
        // Player homes should not be synced, so that chest contents,
        // which are often used as storage, are never accidentally wiped.
        if (!World::Get().GetServerSettings().SyncPlayerHomes && IsPlayerHome(pObjectCell))
            continue;

        GameId cellId{};
        if (!m_world.GetModSystem().GetServerModId(pObjectCell->formID, cellId))
        {
            spdlog::error("Server cell id not found for cell form id {:X}", pObjectCell->formID);
            continue;
        }

        for (TESObjectREFR* pObject : pObjectCell->GetRefsByFormTypes(formTypes))
        {
            const bool cIsHarvestType = pObject->baseForm->formType == FormType::Flora || pObject->baseForm->formType == FormType::Ingredient;
            if (cIsHarvestType && !IsHarvestableObject(pObject))
                continue;

            const bool cIsOpenLoot = ObjectSyncPolicy::IsOpenLootObject(pObject);
            if (ObjectSyncPolicy::IsOpenLootFormType(pObject->baseForm->formType) && !cIsOpenLoot)
                continue;

            if (pObject->baseForm->formType == FormType::Activator && !IsSyncedActivator(pObject))
                continue;

            if (!ShouldSyncObject(pObject, playerStashContainers))
            {
                spdlog::warn("Excluding sync for {:X}", pObject->formID);
                continue;
            }

            ObjectData objectData{};
            objectData.CellId = cellId;
            objectData.WorldSpaceId = worldSpaceId;
            objectData.CurrentCoords = GridCellCoords::CalculateGridCellCoords(pObject->position.x, pObject->position.y);

            if (!m_world.GetModSystem().GetServerModId(pObject->formID, objectData.Id))
            {
                spdlog::error("Server form id not found for object with form id {:X}", pObject->formID);
                continue;
            }

            if (Lock* pLock = pObject->GetLock())
            {
                objectData.CurrentLockData.IsLocked = pLock->IsLocked();
                objectData.CurrentLockData.LockLevel = pLock->lockLevel;
            }

            if (pObject->baseForm->formType == FormType::Container)
                objectData.CurrentInventory = pObject->GetInventory();

            objectData.IsHarvestable = cIsHarvestType;
            objectData.IsHarvestItem = pObject->baseForm->formType == FormType::Ingredient;
            objectData.IsOpenLoot = cIsOpenLoot;
            objectData.IsFurniture = pObject->baseForm->formType == FormType::Furniture;
            objectData.IsDoor = IsSyncedDoor(pObject);
            objectData.IsActivator = IsSyncedActivator(pObject);
            objectData.IsContainer = pObject->baseForm->formType == FormType::Container;

            request.Objects.push_back(objectData);
        }
    }

    spdlog::info("[World] assign objects requested for cell {:X} ({} loaded cell(s)): {} object(s)", pCell->formID, cells.size(), request.Objects.size());
    m_transport.Send(request);
}

void ObjectService::OnAssignObjectsResponse(const AssignObjectsResponse& acMessage) noexcept
{
    for (const ObjectData& objectData : acMessage.Objects)
    {
        const uint32_t cObjectId = World::Get().GetModSystem().GetGameId(objectData.Id);
        TESObjectREFR* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
        if (!pObject)
        {
            spdlog::error("Object not found for form id {:X}", objectData.Id);
            continue;
        }

        CreateObjectEntity(pObject->formID, objectData.ServerId);

        // Late join / re-entry: someone harvested it before we arrived.
        if (objectData.IsHarvestable && objectData.IsHarvested)
            ApplyHarvested(pObject);
        else if (objectData.IsHarvestable)
            RestoreHarvested(pObject);

        if (objectData.IsOpenLoot && objectData.IsLootTaken)
            ApplyWorldItemTaken(pObject);
        else if (objectData.IsOpenLoot)
            RestoreWorldItem(pObject);

        // Late join / re-entry: match the door to the server's open state.
        if (objectData.IsDoor && objectData.IsDoorStateKnown && IsSyncedDoor(pObject))
        {
            const auto cLocalState = pObject->GetOpenState();
            const bool cLocalOpen = cLocalState == TESObjectREFR::kOpen || cLocalState == TESObjectREFR::kOpening;
            if (cLocalOpen != objectData.IsDoorOpen)
            {
                spdlog::info("Door {:X} set {} to match the server", pObject->formID, objectData.IsDoorOpen ? "open" : "closed");
                pObject->SetOpen(objectData.IsDoorOpen);
            }
        }

        // Replay only a bounded approximation of activation history. Trap-like
        // forms are skipped, while binary controls use parity to preserve state.
        if (objectData.IsActivator && IsSyncedActivator(pObject))
        {
            auto& appliedCount = s_appliedActivatorActivationCounts[pObject->formID];
            if (objectData.ActivationCount > appliedCount)
            {
                const char* const pEditorId = pObject->baseForm->GetFormEditorID();
                const auto kind = ActivatorReplayPolicy::Classify(pEditorId ? pEditorId : "");
                const auto replayCount = ActivatorReplayPolicy::ReplayCount(kind, objectData.ActivationCount, appliedCount);
                if (replayCount == 0)
                {
                    appliedCount = objectData.ActivationCount;
                    if (kind == ActivatorReplayPolicy::Kind::kNeverReplay)
                        spdlog::info("Activator {:X} history skipped as trap/hazard-like (editor id '{}')", pObject->formID, pEditorId ? pEditorId : "");
                }
                else
                {
                    PlayerCharacter* pPlayer = PlayerCharacter::Get();
                    if (!pPlayer)
                    {
                        spdlog::warn("Activator {:X} state cannot be replayed before the local player exists", pObject->formID);
                    }
                    else
                    {
                        {
                            ScopedActivatorStateReplay replay(pObject->formID);
                            for (std::uint32_t index = 0; index < replayCount; ++index)
                                pObject->Activate(pPlayer, 0, nullptr, 1, 0);
                        }
                        appliedCount = objectData.ActivationCount;
                        spdlog::info("Activator {:X} replayed {} bounded server activation(s) (editor id '{}')", pObject->formID, replayCount, pEditorId ? pEditorId : "");
                    }
                }
            }
        }

        if (objectData.IsStateUntrusted)
            continue;

        if (objectData.CurrentLockData != LockData{})
        {
            Lock* pLock = pObject->GetLock();

            if (!pLock)
            {
                pLock = pObject->CreateLock();
                if (!pLock)
                    continue;
            }

            pLock->lockLevel = objectData.CurrentLockData.LockLevel;
            pLock->SetLock(objectData.CurrentLockData.IsLocked);
            pObject->LockChange();
        }

        if (pObject->baseForm->formType == FormType::Container)
        {
            Inventory currentInventory = pObject->GetInventory();

            if (currentInventory.ContainsQuestItems())
                pObject->SetInventoryRetainingQuestItems(currentInventory, objectData.CurrentInventory);
            else
                pObject->SetInventory(objectData.CurrentInventory);
        }
    }
}

entt::entity ObjectService::CreateObjectEntity(const uint32_t acFormId, const uint32_t acServerId) noexcept
{
    const auto view = m_world.view<FormIdComponent, ObjectComponent>();

    // One entity per form: the server prunes unobserved objects and hands out a new id when the
    // object is discovered again, so an older entity for this form must be re-pointed, not duplicated.
    // Otherwise lookups by form id (containers, activation) keep resolving to the dead server id.
    auto it = std::find_if(view.begin(), view.end(), [acFormId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == acFormId; });

    if (it != view.end())
    {
        auto& objectComponent = view.get<ObjectComponent>(*it);
        if (objectComponent.Id != acServerId)
        {
            spdlog::info("Object entity for form {:X} re-pointed from server id {:X} to {:X}", acFormId, objectComponent.Id, acServerId);
            objectComponent.Id = acServerId;
        }
        return *it;
    }

    entt::entity entity = m_world.create();
    spdlog::info("Created object entity, server id: {:X}, form id {:X}", acServerId, acFormId);

    m_world.emplace<FormIdComponent>(entity, acFormId);
    m_world.emplace<ObjectComponent>(entity, acServerId);

    return entity;
}

void ObjectService::OnActivate(const ActivateEvent& acEvent) noexcept
{
    if (acEvent.pObject && s_replayingActivatorFormId == acEvent.pObject->formID)
    {
        if (acEvent.ActivateFlag)
            acEvent.pObject->Activate(acEvent.pActivator, acEvent.Unk1, acEvent.pObjectToGet, acEvent.Count, acEvent.DefaultProcessing);
        return;
    }

    const bool wasDisabled = acEvent.pObject && acEvent.pObject->IsDisabled();
    const bool isLocalHarvest = ObjectSyncPolicy::ShouldTrackLocalHarvest(
        IsHarvestableObject(acEvent.pObject),
        acEvent.pActivator == PlayerCharacter::Get(),
        wasDisabled);
    const bool trackLocalHarvest = WorldObjectTrackingPolicy::ShouldTrackHarvest(
        m_transport.IsConnected(),
        acEvent.ActivateFlag && isLocalHarvest);

    // Remembered before Activate: the sit action can be performed inside it.
    if (acEvent.pObject && acEvent.pObject->baseForm && acEvent.pObject->baseForm->formType == FormType::Furniture &&
        acEvent.pActivator && acEvent.pActivator == PlayerCharacter::Get())
    {
        auto* pExtension = acEvent.pActivator->GetExtension();
        pExtension->PendingFurnitureFormId = acEvent.pObject->formID;
        pExtension->PendingFurnitureTick = World::Get().GetTick();
    }

    if (acEvent.ActivateFlag)
    {
        acEvent.pObject->Activate(acEvent.pActivator, acEvent.Unk1, acEvent.pObjectToGet, acEvent.Count, acEvent.DefaultProcessing);
    }

    // The harvesting client is excluded from the server's relay and its game may disable the plant locally.
    // Track this reference so the later server respawn notification can re-enable it.
    if (trackLocalHarvest)
        s_harvestDisabledRefs.insert(acEvent.pObject->formID);

    const bool cIsSyncedDoor = IsSyncedDoor(acEvent.pObject);
    const bool cIsSyncedActivator = IsSyncedActivator(acEvent.pObject);
    const bool cIsTrackedActivation = cIsSyncedDoor || cIsSyncedActivator;

    if (!m_transport.IsConnected())
    {
        if (cIsTrackedActivation)
            spdlog::warn("[World] activation not sent: transport is disconnected (object {:X})", acEvent.pObject->formID);
        return;
    }

    // A relayed activator runs with the original player's actor on observers.
    // Do not echo that remote replay back as a fresh request from this client.
    if ((cIsSyncedDoor || cIsSyncedActivator) && acEvent.pActivator != PlayerCharacter::Get())
        return;

    if (cIsTrackedActivation)
        spdlog::info("[World] captured {} activation for form {:X} (state {}, actor form {:X})", cIsSyncedDoor ? "door" : "activator", acEvent.pObject->formID, static_cast<uint8_t>(acEvent.PreActivationOpenState), acEvent.pActivator->formID);

    if (Lock* pLock = acEvent.pObject->GetLock())
    {
        if (pLock->flags & 0xFF)
        {
            if (cIsTrackedActivation)
                spdlog::info("[World] activation not sent: object {:X} is locked", acEvent.pObject->formID);
            return;
        }
    }

    ActivateRequest request;

    if (!m_world.GetModSystem().GetServerModId(acEvent.pObject->formID, request.Id))
    {
        spdlog::error("Server form id not found for object form id {:X}", acEvent.pObject->formID);
        return;
    }

    TESObjectCELL* pCell = acEvent.pObject->GetParentCellEx();
    if (!pCell)
    {
        spdlog::error("Activated object has no parent cell: {:X}", acEvent.pObject->formID);
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(pCell->formID, request.CellId))
    {
        spdlog::error("Server cell id not found for cell form id {:X}", pCell->formID);
        return;
    }

    auto view = m_world.view<FormIdComponent>();
    const auto pEntity = std::find_if(std::begin(view), std::end(view), [id = acEvent.pActivator->formID, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (pEntity == std::end(view))
    {
        if (cIsTrackedActivation)
            spdlog::warn("[World] activation not sent: local actor {:X} has no server entity (object {:X})", acEvent.pActivator->formID, acEvent.pObject->formID);
        return;
    }

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*pEntity);
    if (!serverIdRes.has_value())
    {
        if (cIsTrackedActivation)
            spdlog::warn("[World] activation not sent: local actor {:X} has no server id (object {:X})", acEvent.pActivator->formID, acEvent.pObject->formID);
        return;
    }

    request.ActivatorId = serverIdRes.value();
    request.PreActivationOpenState = acEvent.PreActivationOpenState;

    m_transport.Send(request);

    if (cIsSyncedActivator)
        IncrementAppliedActivatorCount(acEvent.pObject->formID);

    if (cIsTrackedActivation)
        spdlog::info("[World] sent {} activation for form {:X} (server id {:X}:{:X}, cell {:X}:{:X}, pre-state {}, actor {:X})", cIsSyncedDoor ? "door" : "activator", acEvent.pObject->formID, request.Id.ModId, request.Id.BaseId, request.CellId.ModId, request.CellId.BaseId, request.PreActivationOpenState, request.ActivatorId);
}

void ObjectService::OnActivateNotify(const NotifyActivate& acMessage) noexcept
{
    const uint32_t cObjectId = World::Get().GetModSystem().GetGameId(acMessage.Id);
    TESObjectREFR* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
    if (!pObject)
    {
        spdlog::error("[World] remote activation could not find object {:X}:{:X} (local form {:X})", acMessage.Id.ModId, acMessage.Id.BaseId, cObjectId);
        return;
    }

    if (IsSyncedDoor(pObject))
    {
        const auto cPreActivationState = static_cast<TESObjectREFR::OpenState>(acMessage.PreActivationOpenState);
        bool cWasOpen = false;
        if (cPreActivationState == TESObjectREFR::kOpen || cPreActivationState == TESObjectREFR::kOpening)
            cWasOpen = true;
        else if (cPreActivationState != TESObjectREFR::kClosed && cPreActivationState != TESObjectREFR::kClosing)
        {
            spdlog::warn("[World] ignored door notification for {:X}:{:X}: invalid pre-state {}", acMessage.Id.ModId, acMessage.Id.BaseId, acMessage.PreActivationOpenState);
            return;
        }

        const bool cTargetOpen = !cWasOpen;
        const auto cLocalState = pObject->GetOpenState();
        const bool cLocalOpen = cLocalState == TESObjectREFR::kOpen || cLocalState == TESObjectREFR::kOpening;
        if (cLocalOpen != cTargetOpen)
        {
            // The server accepted this toggle, so apply its resulting state directly.
            // Replaying Activate is timing-sensitive and can leave observers behind.
            pObject->SetOpen(cTargetOpen);
        }

        spdlog::info("[World] applied remote door activation {:X}:{:X} (local form {:X}, pre-state {}, local state {}, now {})", acMessage.Id.ModId, acMessage.Id.BaseId, pObject->formID, acMessage.PreActivationOpenState, static_cast<uint8_t>(cLocalState), cTargetOpen ? "open" : "closed");
        return;
    }

    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ActivatorId);
    if (!pActor)
    {
        spdlog::error("[World] remote activation for {:X}:{:X} could not find actor server id {:X}", acMessage.Id.ModId, acMessage.Id.BaseId, acMessage.ActivatorId);
        return;
    }

    // unsure if these flags are the best, but these are passed with the papyrus Activate fn
    // might be an idea to have the client send the flags through NotifyActivate
    pObject->Activate(pActor, 0, nullptr, 1, 0);

    if (IsSyncedActivator(pObject))
    {
        IncrementAppliedActivatorCount(pObject->formID);
        spdlog::info("[World] applied remote activator activation {:X}:{:X} (local form {:X}, actor {:X})", acMessage.Id.ModId, acMessage.Id.BaseId, pObject->formID, acMessage.ActivatorId);
    }
}

void ObjectService::OnObjectHarvestedNotify(const NotifyObjectHarvested& acMessage) noexcept
{
    const uint32_t cObjectId = World::Get().GetModSystem().GetGameId(acMessage.Id);
    TESObjectREFR* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
    if (!pObject)
    {
        spdlog::error("{}: object not found for form id {:X}", __FUNCTION__, cObjectId);
        return;
    }

    if (acMessage.IsHarvested)
        ApplyHarvested(pObject);
    else
        RestoreHarvested(pObject);
}

void ObjectService::OnWorldItemTakenNotify(const NotifyWorldItemTaken& acMessage) noexcept
{
    const uint32_t cObjectId = World::Get().GetModSystem().GetGameId(acMessage.Id);
    TESObjectREFR* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
    if (!pObject)
    {
        spdlog::error("{}: world item not found for form id {:X}", __FUNCTION__, cObjectId);
        return;
    }

    if (acMessage.IsTaken)
        ApplyWorldItemTaken(pObject);
    else
        RestoreWorldItem(pObject);
}

void ObjectService::OnLockChange(const LockChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    LockChangeRequest request;

    if (!m_world.GetModSystem().GetServerModId(acEvent.FormId, request.Id))
    {
        spdlog::error("Server form id for lock object not found, form id: {:X}", acEvent.FormId);
        return;
    }

    const auto* const pObject = Cast<TESObjectREFR>(TESForm::GetById(acEvent.FormId));

    TESObjectCELL* pCell = pObject->GetParentCellEx();
    if (!pCell)
    {
        spdlog::error("Activated object has no parent cell: {:X}", pObject->formID);
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(pCell->formID, request.CellId))
    {
        spdlog::error("Server cell id for cell not found, cell form id: {:X}", pCell->formID);
        return;
    }

    request.IsLocked = acEvent.IsLocked;
    request.LockLevel = acEvent.LockLevel;

    const std::pair<bool, uint8_t> cLockState{request.IsLocked, static_cast<uint8_t>(request.LockLevel)};
    const auto [it, inserted] = s_sentLockStates.try_emplace(acEvent.FormId, cLockState);
    if (!inserted)
    {
        if (it->second == cLockState)
            return;
        it->second = cLockState;
    }

    m_transport.Send(request);
}

void ObjectService::OnLockChangeNotify(const NotifyLockChange& acMessage) noexcept
{
    const auto cObjectId = World::Get().GetModSystem().GetGameId(acMessage.Id);
    if (cObjectId == 0)
    {
        spdlog::error("Failed to retrieve object id to (un)lock.");
        return;
    }

    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
    if (!pObject)
    {
        spdlog::error("Failed to retrieve object to (un)lock.");
        return;
    }

    auto* pLock = pObject->GetLock();

    if(!acMessage.IsLocked)
    {
        if (!pLock || !pLock->IsLocked())
            return;
    }

    if (!pLock && acMessage.IsLocked)
    {
        pLock = pObject->CreateLock();
        if (!pLock)
        {
            spdlog::error("Failed to create lock for object form id {:X}", pObject->formID);
            return;
        }
    }

    pLock->lockLevel = acMessage.LockLevel;
    pLock->SetLock(acMessage.IsLocked);
    pObject->LockChange();
}

void ObjectService::OnScriptAnimationEvent(const ScriptAnimationEvent& acEvent) noexcept
{
    const auto view = m_world.view<FormIdComponent>();
    const auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.FormID](const auto entity)
    {
        return view.get<FormIdComponent>(entity).Id == formId;
    });
    if (entityIt == view.end())
        return;

    const auto serverId = Utils::GetServerId(*entityIt);
    if (!serverId)
        return;

    ScriptAnimationRequest request{};
    request.ServerId = serverId.value();
    request.Animation = acEvent.Animation;
    request.EventName = acEvent.EventName;

    m_transport.Send(request);
}

void ObjectService::OnNotifyScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept
{
    if (!acMessage.FormID)
        return;

    const auto formId = m_world.GetModSystem().GetGameId(acMessage.FormID);
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(formId));

    if (!pObject)
    {
        spdlog::error("Failed to fetch notify script animation object, form id: {:X}", formId);
        return;
    }

    BSFixedString eventName(acMessage.EventName.c_str());
    if (acMessage.Animation == String{})
    {
        pObject->PlayAnimation(&eventName);
    }
    else
    {
        BSFixedString animation(acMessage.Animation.c_str());
        pObject->PlayAnimationAndWait(&animation, &eventName);
    }
}

BSTEventResult ObjectService::OnEvent(const TESActivateEvent* acEvent, const EventDispatcher<TESActivateEvent>* aDispatcher)
{
#if ENVIRONMENT_DEBUG
    auto view = m_world.view<ObjectComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acEvent->object->formID, view](entt::entity entity) { return view.get<ObjectComponent>(entity).Id == id; });

    if (itor == std::end(view))
    {
        AddObjectComponent(acEvent->object);
    }
#endif

    return BSTEventResult::kOk;
}
