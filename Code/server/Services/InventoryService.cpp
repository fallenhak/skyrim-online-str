#include "InventoryService.h"
#include <Services/DropLog.h>

#include <Components.h>
#include <World.h>
#include <GameServer.h>
#include <Services/InventoryInteractionPolicy.h>
#include <Services/ObjectInteractionPolicy.h>
#include <Services/ObjectService.h>

#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/RequestContainerTransfer.h>
#include <Messages/NotifyContainerTransferResult.h>

#include <chrono>

#include <Setting.h>
namespace
{
Console::Setting bEnableItemDrops{"Gameplay:bEnableItemDrops", "(Experimental) Syncs dropped items by players", false};
}

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_inventoryChangeConnection = aDispatcher.sink<PacketEvent<RequestInventoryChanges>>().connect<&InventoryService::OnInventoryChanges>(this);
    m_equipmentChangeConnection = aDispatcher.sink<PacketEvent<RequestEquipmentChanges>>().connect<&InventoryService::OnEquipmentChanges>(this);
    m_drawWeaponConnection = aDispatcher.sink<PacketEvent<DrawWeaponRequest>>().connect<&InventoryService::OnWeaponDrawnRequest>(this);
    m_containerTransferConnection = aDispatcher.sink<PacketEvent<RequestContainerTransfer>>().connect<&InventoryService::OnContainerTransfer>(this);
}

void InventoryService::OnInventoryChanges(const PacketEvent<RequestInventoryChanges>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    if (!InventoryInteractionPolicy::HasValidItemPayload(message.Item))
    {
        DropLog::Info("inventory change: malformed item", "player {:X}, entity {:X}", acMessage.pPlayer->GetId(), message.ServerId);
        return;
    }

    auto view = m_world.view<InventoryComponent>();

    const auto it = view.find(static_cast<entt::entity>(message.ServerId));

    if (it == view.end())
    {
        DropLog::Info("inventory change: entity not found", "player {:X}, entity {:X}", acMessage.pPlayer->GetId(), message.ServerId);
        return;
    }

    const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(*it);
    const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(*it);

    // Once looting started, a retained corpse's inventory changes only through RequestContainerTransfer;
    // the owner's broadcast would race the server copy.
    if (const auto* pCorpseMarker = pCharacterComponent ? m_world.try_get<CorpseRetentionComponent>(*it) : nullptr;
        pCorpseMarker && ContainerTransferPolicy::IgnoresOwnerInventoryBroadcast(
                             pCharacterComponent->IsPlayer(), pCharacterComponent->IsDead(), true, pCorpseMarker->RemovalQueued,
                             pCorpseMarker->OwnerSeedClosed))
    {
        spdlog::debug("Ignored inventory change from player {:X} for retained corpse {:X}", acMessage.pPlayer->GetId(), message.ServerId);
        return;
    }

    const auto* pCellComponent = m_world.try_get<CellIdComponent>(*it);
    const auto* pPersistentCharacterComponent = m_world.try_get<PersistentCharacterComponent>(*it);
    const auto* pObjectComponent = m_world.try_get<ObjectComponent>(*it);
    const bool isObject = pObjectComponent != nullptr;
    // Container contents change only through RequestContainerTransfer; the two-message
    // path (container -N, player +N) cannot be made atomic and would allow duplication.
    const bool hasTrustedObjectState = pObjectComponent && pObjectComponent->HasTrustedState && !pObjectComponent->IsContainer;
    const bool hasOwner = pOwnerComponent && pOwnerComponent->GetOwner();
    const bool isCurrentOwner = pOwnerComponent && pOwnerComponent->IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch);
    const bool ownershipEpochMatches = pOwnerComponent
        ? message.OwnershipEpoch != 0 && pOwnerComponent->OwnershipEpoch == message.OwnershipEpoch
        : message.OwnershipEpoch == 0;
    const auto& senderCell = acMessage.pPlayer->GetCellComponent();
    const bool isInRange = hasOwner && pCharacterComponent && pCellComponent && senderCell.IsInRange(*pCellComponent, pCharacterComponent->IsDragon());
    const bool isObjectInRange = isObject && pCellComponent && ObjectInteractionPolicy::CanInteract(
        pCellComponent->Cell, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
        pCellComponent->Cell, pCellComponent->WorldSpaceId, pCellComponent->CenterCoords);
    const bool isInAuthorizedRange = isInRange || isObjectInRange;

    if (!InventoryInteractionPolicy::IsAuthorized(
            hasOwner,
            isCurrentOwner,
            ownershipEpochMatches,
            isObject,
            hasTrustedObjectState,
            pCharacterComponent != nullptr,
            pCharacterComponent && pCharacterComponent->IsPlayer(),
            pPersistentCharacterComponent != nullptr,
            isInAuthorizedRange))
    {
        const uint32_t ownerId = pOwnerComponent && pOwnerComponent->GetOwner() ? pOwnerComponent->GetOwner()->GetId() : 0;
        DropLog::Info("inventory change: not authorized",
            "player {:X}, entity {:X}; owner {:X}, epoch {} (current {}), object {} (trusted {}), character {}, persistent {}, in range {}",
            acMessage.pPlayer->GetId(), message.ServerId, ownerId, message.OwnershipEpoch, pOwnerComponent ? pOwnerComponent->OwnershipEpoch : 0,
            isObject, hasTrustedObjectState, pCharacterComponent != nullptr, pPersistentCharacterComponent != nullptr, isInAuthorizedRange);
        return;
    }

    const bool isRemoteNpcInteraction = hasOwner && !isCurrentOwner;

    auto& inventoryComponent = view.get<InventoryComponent>(*it);
    if (!InventoryInteractionPolicy::CanApplyItem(inventoryComponent.Content, message.Item))
    {
        DropLog::Info("inventory change: cannot apply item", "player {:X}, entity {:X}, item {:X}:{:X} x{}", acMessage.pPlayer->GetId(), message.ServerId, message.Item.BaseId.ModId, message.Item.BaseId.BaseId, message.Item.Count);
        return;
    }

    inventoryComponent.Content.AddOrRemoveEntry(message.Item);

    if (!InventoryInteractionPolicy::ShouldNotifyClients(message.UpdateClients, isRemoteNpcInteraction))
        return;

    NotifyInventoryChanges notify;
    notify.ServerId = message.ServerId;
    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.Item = message.Item;

    notify.Drop = InventoryInteractionPolicy::ShouldRelayDrop(message.Drop, bEnableItemDrops, isRemoteNpcInteraction);

    const entt::entity cOrigin = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cOrigin, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void InventoryService::OnEquipmentChanges(const PacketEvent<RequestEquipmentChanges>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    auto view = m_world.view<InventoryComponent>();

    const auto it = view.find(static_cast<entt::entity>(message.ServerId));

    if (it == view.end())
    {
        DropLog::Info("equipment change: entity not found", "player {:X}, entity {:X}", acMessage.pPlayer->GetId(), message.ServerId);
        return;
    }

    const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(*it);
    const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(*it);
    const bool hasOwner = pOwnerComponent && pOwnerComponent->GetOwner();
    const bool isOwnedBySender = hasOwner && pOwnerComponent->GetOwner() == acMessage.pPlayer;
    const uint32_t currentEpoch = pOwnerComponent ? pOwnerComponent->OwnershipEpoch : 0;
    if (!InventoryInteractionPolicy::CanChangeEquipment(
            pCharacterComponent != nullptr, hasOwner, isOwnedBySender,
            message.OwnershipEpoch, currentEpoch))
    {
        const uint32_t ownerId = hasOwner ? pOwnerComponent->GetOwner()->GetId() : 0;
        DropLog::Info("equipment change: not authorized",
            "player {:X}, entity {:X}; character {}, owner {:X}, requested epoch {} (current {})",
            acMessage.pPlayer->GetId(), message.ServerId, pCharacterComponent != nullptr,
            ownerId, message.OwnershipEpoch, currentEpoch);
        return;
    }

    auto& inventoryComponent = view.get<InventoryComponent>(*it);
    inventoryComponent.Content.UpdateEquipment(message.CurrentInventory);

    NotifyEquipmentChanges notify;
    notify.ServerId = message.ServerId;
    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.ItemId = message.ItemId;
    notify.EquipSlotId = message.EquipSlotId;
    notify.Count = message.Count;
    notify.Unequip = message.Unequip;
    notify.IsSpell = message.IsSpell;
    notify.IsShout = message.IsShout;

    const entt::entity cOrigin = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cOrigin, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void InventoryService::OnWeaponDrawnRequest(const PacketEvent<DrawWeaponRequest>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto it = characterView.find(static_cast<entt::entity>(message.Id));

    if (it == std::end(characterView))
    {
        DropLog::Info("weapon drawn: actor not found", "player {:X}, actor {:X}", acMessage.pPlayer->GetId(), message.Id);
        return;
    }

    auto& ownerComponent = characterView.get<OwnerComponent>(*it);
    if (!ownerComponent.IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
    {
        DropLog::Info("weapon drawn: not owner",
            "player {:X}, actor {:X}; requested epoch {} does not match current epoch {}",
            acMessage.pPlayer->GetId(), message.Id, message.OwnershipEpoch, ownerComponent.OwnershipEpoch);
        return;
    }

    auto& characterComponent = characterView.get<CharacterComponent>(*it);
    characterComponent.SetWeaponDrawn(message.IsWeaponDrawn);
    spdlog::debug("Updating weapon drawn state {:x}:{} at epoch {}", message.Id, message.IsWeaponDrawn, message.OwnershipEpoch);
}

void InventoryService::OnContainerTransfer(const PacketEvent<RequestContainerTransfer>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;
    Player* const pPlayer = acMessage.pPlayer;

    NotifyContainerTransferResult reply;
    reply.RequestId = message.RequestId;

    auto& session = m_transferSessions[pPlayer->GetId()];
    const uint64_t nowSecond = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count());

    const auto target = static_cast<ContainerTransferTarget>(message.TargetKind);
    const bool isCorpseTarget = target == ContainerTransferTarget::kCorpse;
    const auto containerEntity = static_cast<entt::entity>(message.ContainerId);
    const bool isContainerValid = (target == ContainerTransferTarget::kObject || isCorpseTarget) && m_world.valid(containerEntity);
    auto* pObjectComponent = isContainerValid && !isCorpseTarget ? m_world.try_get<ObjectComponent>(containerEntity) : nullptr;
    const auto* pCorpseCharacter = isContainerValid && isCorpseTarget ? m_world.try_get<CharacterComponent>(containerEntity) : nullptr;
    auto* pCorpseMarker = pCorpseCharacter ? m_world.try_get<CorpseRetentionComponent>(containerEntity) : nullptr;
    const bool hasContainer = pObjectComponent || pCorpseCharacter;
    auto* pContainerInventory = hasContainer ? m_world.try_get<InventoryComponent>(containerEntity) : nullptr;
    const auto* pContainerCell = hasContainer ? m_world.try_get<CellIdComponent>(containerEntity) : nullptr;

    const auto characterEntity = pPlayer->GetCharacter();
    auto* pPlayerInventory = characterEntity ? m_world.try_get<InventoryComponent>(*characterEntity) : nullptr;
    const auto* pCharacterCell = characterEntity ? m_world.try_get<CellIdComponent>(*characterEntity) : nullptr;

    Inventory scratchContainer{};
    Inventory scratchPlayer{};
    const bool hasInventories = pContainerInventory && pPlayerInventory && pContainerCell && pCharacterCell;

    bool allowed = false;
    if (hasInventories)
    {
        const auto& senderCell = pPlayer->GetCellComponent();
        const bool inRange = ObjectInteractionPolicy::CanInteract(
                                 pContainerCell->Cell, senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
                                 pContainerCell->Cell, pContainerCell->WorldSpaceId, pContainerCell->CenterCoords) &&
            ObjectInteractionPolicy::IsInSenderRange(
                                 pCharacterCell->Cell, pCharacterCell->WorldSpaceId, pCharacterCell->CenterCoords,
                                 pContainerCell->Cell, pContainerCell->WorldSpaceId, pContainerCell->CenterCoords);

        if (pObjectComponent)
            allowed = pObjectComponent->IsContainer && pObjectComponent->HasTrustedState && inRange;
        else
            allowed = ContainerTransferPolicy::IsCorpseTransferAllowed(
                pCorpseCharacter->IsPlayer(), pCorpseCharacter->IsDead(), pCorpseMarker && pCorpseMarker->RemovalQueued,
                static_cast<ContainerTransferDirection>(message.Direction), inRange);
    }

    bool applied = false;
    const auto result = ContainerTransferPolicy::TryTransfer(
        session, message.RequestId, nowSecond, allowed, static_cast<ContainerTransferDirection>(message.Direction), message.Item,
        message.ExpectedContainerCount, hasInventories ? pContainerInventory->Content : scratchContainer,
        hasInventories ? pPlayerInventory->Content : scratchPlayer, &applied);

    reply.Result = static_cast<uint8_t>(result);
    pPlayer->Send(reply);

    if (result != ContainerTransferResult::kAccepted)
    {
        spdlog::info(
            "Container transfer {} from player {:X} rejected ({}): {} {:X}, item {:X} x{}", message.RequestId, pPlayer->GetId(),
            static_cast<int>(result), isCorpseTarget ? "corpse" : "container", message.ContainerId, message.Item.BaseId.BaseId, message.Item.Count);
        return;
    }

    // A replayed id is answered again but must not be relayed twice.
    if (!applied || !hasInventories)
        return;

    if (pCorpseMarker)
        pCorpseMarker->OwnerSeedClosed = true;

    // Object containers outlive the session; retained corpses are transient and are not persisted.
    if (!isCorpseTarget)
        m_world.ctx().at<ObjectService>().PersistContainerContents(containerEntity);

    const bool isTake = static_cast<ContainerTransferDirection>(message.Direction) == ContainerTransferDirection::kTake;

    NotifyInventoryChanges containerNotify;
    containerNotify.ServerId = message.ContainerId;
    // Clients resolve actor inventory updates by server id and ownership epoch.
    if (const auto* pCorpseOwner = isCorpseTarget ? m_world.try_get<OwnerComponent>(containerEntity) : nullptr)
        containerNotify.OwnershipEpoch = pCorpseOwner->OwnershipEpoch;
    containerNotify.Item = message.Item;
    containerNotify.Item.Count = isTake ? -message.Item.Count : message.Item.Count;
    if (!GameServer::Get()->SendToPlayersInRange(containerNotify, containerEntity, pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed for container", __FUNCTION__);

    if (const auto* pOwner = m_world.try_get<OwnerComponent>(*characterEntity))
    {
        NotifyInventoryChanges playerNotify;
        playerNotify.ServerId = World::ToInteger(*characterEntity);
        playerNotify.OwnershipEpoch = pOwner->OwnershipEpoch;
        playerNotify.Item = message.Item;
        playerNotify.Item.Count = isTake ? message.Item.Count : -message.Item.Count;
        if (!GameServer::Get()->SendToPlayersInRange(playerNotify, *characterEntity, pPlayer))
            spdlog::error("{}: SendToPlayersInRange failed for character", __FUNCTION__);
    }
}
