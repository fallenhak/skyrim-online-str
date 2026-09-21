#include "InventoryService.h"

#include <Components.h>
#include <World.h>
#include <GameServer.h>
#include <Services/InventoryInteractionPolicy.h>

#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/DrawWeaponRequest.h>

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
}

void InventoryService::OnInventoryChanges(const PacketEvent<RequestInventoryChanges>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    if (!InventoryInteractionPolicy::HasValidItemPayload(message.Item))
    {
        spdlog::debug("Rejected malformed inventory change from player {:X} for entity {:X}", acMessage.pPlayer->GetId(), message.ServerId);
        return;
    }

    auto view = m_world.view<InventoryComponent>();

    const auto it = view.find(static_cast<entt::entity>(message.ServerId));

    if (it == view.end())
        return;

    const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(*it);
    const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(*it);
    const auto* pCellComponent = m_world.try_get<CellIdComponent>(*it);
    const auto* pPersistentCharacterComponent = m_world.try_get<PersistentCharacterComponent>(*it);
    const bool isObject = m_world.all_of<ObjectComponent>(*it);
    const bool hasOwner = pOwnerComponent && pOwnerComponent->GetOwner();
    const bool isCurrentOwner = pOwnerComponent && pOwnerComponent->IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch);
    const bool ownershipEpochMatches = pOwnerComponent
        ? message.OwnershipEpoch != 0 && pOwnerComponent->OwnershipEpoch == message.OwnershipEpoch
        : message.OwnershipEpoch == 0;
    const bool isInRange = hasOwner && pCharacterComponent && pCellComponent && acMessage.pPlayer->GetCellComponent().IsInRange(*pCellComponent, pCharacterComponent->IsDragon());

    if (!InventoryInteractionPolicy::IsAuthorized(
            hasOwner,
            isCurrentOwner,
            ownershipEpochMatches,
            isObject,
            pCharacterComponent != nullptr,
            pCharacterComponent && pCharacterComponent->IsPlayer(),
            pPersistentCharacterComponent != nullptr,
            isInRange))
    {
        const uint32_t ownerId = pOwnerComponent && pOwnerComponent->GetOwner() ? pOwnerComponent->GetOwner()->GetId() : 0;
        spdlog::debug(
            "Rejected inventory change from player {:X} for entity {:X}; owner {:X}, epoch {} (current {}), object {}, character {}, persistent {}, in range {}",
            acMessage.pPlayer->GetId(), message.ServerId, ownerId, message.OwnershipEpoch, pOwnerComponent ? pOwnerComponent->OwnershipEpoch : 0,
            isObject, pCharacterComponent != nullptr, pPersistentCharacterComponent != nullptr, isInRange);
        return;
    }

    const bool isRemoteNpcInteraction = hasOwner && !isCurrentOwner;

    auto& inventoryComponent = view.get<InventoryComponent>(*it);
    if (!InventoryInteractionPolicy::CanApplyItem(inventoryComponent.Content, message.Item))
    {
        spdlog::debug("Rejected inventory change from player {:X} for entity {:X}: item cannot be applied to the current inventory", acMessage.pPlayer->GetId(), message.ServerId);
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
        return;

    const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(*it);
    if (pOwnerComponent)
    {
        if (pOwnerComponent->GetOwner() != acMessage.pPlayer || pOwnerComponent->OwnershipEpoch != message.OwnershipEpoch)
        {
            const uint32_t ownerId = pOwnerComponent->GetOwner() ? pOwnerComponent->GetOwner()->GetId() : 0;
            spdlog::debug(
                "Rejected equipment change from player {:X} for actor {:X}; current owner is {:X} and requested epoch {} does not match {}",
                acMessage.pPlayer->GetId(), message.ServerId, ownerId, message.OwnershipEpoch, pOwnerComponent->OwnershipEpoch);
            return;
        }
    }
    else if (message.OwnershipEpoch != 0)
    {
        spdlog::warn(
            "Rejected equipment change from player {:X} because object {:X} unexpectedly carried ownership epoch {}",
            acMessage.pPlayer->GetId(), message.ServerId, message.OwnershipEpoch);
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

    if (it != std::end(characterView) && characterView.get<OwnerComponent>(*it).GetOwner() == acMessage.pPlayer)
    {
        auto& characterComponent = characterView.get<CharacterComponent>(*it);
        characterComponent.SetWeaponDrawn(message.IsWeaponDrawn);
        spdlog::debug("Updating weapon drawn state {:x}:{}", message.Id, message.IsWeaponDrawn);
    }
}
