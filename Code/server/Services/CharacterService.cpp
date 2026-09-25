#include <Services/CharacterService.h>
#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Events/CharacterSpawnedEvent.h>
#include <Events/CharacterExteriorCellChangeEvent.h>
#include <Events/CharacterInteriorCellChangeEvent.h>
#include <Events/PlayerEnterWorldEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/CharacterRemoveEvent.h>
#include <Events/OwnershipTransferEvent.h>
#include <Events/ActorRespawnedEvent.h>
#include <Events/AcceptedCanonicalCreatureDeathEvent.h>

#include <Game/OwnerView.h>

#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/NotifyCharacterReadyResult.h>
#include <Messages/NotifyCharacterEnteredWorld.h>
#include <Messages/NotifyCharacterAssignmentRejected.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/NotifyFurnitureUseDenied.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Services/CharacterRemoval.h>
#include <Messages/RequestOwnershipTransfer.h>
#include <Messages/NotifyOwnershipTransfer.h>
#include <Messages/RequestOwnershipClaim.h>
#include <Messages/MountRequest.h>
#include <Messages/NotifyMount.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/RequestRespawn.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/DialogueRequest.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifyActorTeleport.h>
#include <Structs/FactionAuthorityPolicy.h>
#include <Structs/CellMovementAuthorityPolicy.h>
#include <Structs/MovementAuthorityPolicy.h>
#include <Services/FurnitureUsePolicy.h>
#include <Services/ObjectInteractionPolicy.h>
#include <Services/CorpseRetentionPolicy.h>
#include <Services/PresentationAuthorityPolicy.h>

#include <cmath>

namespace
{
constexpr std::uint32_t kHealthActorValue = 24;
constexpr std::uint32_t kMagickaActorValue = 25;
constexpr std::uint32_t kStaminaActorValue = 26;

bool CanRelayNpcPresentation(World& aWorld, const Player& acSender, const uint32_t aServerId) noexcept
{
    const auto source = static_cast<entt::entity>(aServerId);
    if (!aWorld.valid(source))
        return false;

    const auto* pCharacterComponent = aWorld.try_get<CharacterComponent>(source);
    const auto* pFormIdComponent = aWorld.try_get<FormIdComponent>(source);
    const auto* pCellComponent = aWorld.try_get<CellIdComponent>(source);
    const bool isNpcCharacter = pCharacterComponent && !pCharacterComponent->IsPlayer();
    const bool hasFormId = pFormIdComponent && pFormIdComponent->Id.BaseId != 0;
    const auto& senderCell = acSender.GetCellComponent();
    const bool hasCell = pCellComponent && static_cast<bool>(*pCellComponent) && static_cast<bool>(senderCell);
    const bool isInRange = hasCell && pCharacterComponent && ObjectInteractionPolicy::IsInSenderRange(
        senderCell.Cell, senderCell.WorldSpaceId, senderCell.CenterCoords,
        pCellComponent->Cell, pCellComponent->WorldSpaceId, pCellComponent->CenterCoords,
        pCharacterComponent->IsDragon());

    return PresentationAuthorityPolicy::CanRelayNpcPresentation(
        true, isNpcCharacter, hasFormId, hasCell, isInRange);
}
}

CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher, const std::uint32_t aCreatureCorpseLifetimeSeconds) noexcept
    : m_world(aWorld)
    , m_creatureCorpseLifetimeSeconds(aCreatureCorpseLifetimeSeconds)
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&CharacterService::OnUpdate>(this))
    , m_canonicalCreatureDeathConnection(aDispatcher.sink<AcceptedCanonicalCreatureDeathEvent>().connect<&CharacterService::OnAcceptedCanonicalCreatureDeath>(this))
    , m_exteriorCellChangeEventConnection(aDispatcher.sink<CharacterExteriorCellChangeEvent>().connect<&CharacterService::OnCharacterExteriorCellChange>(this))
    , m_interiorCellChangeEventConnection(aDispatcher.sink<CharacterInteriorCellChangeEvent>().connect<&CharacterService::OnCharacterInteriorCellChange>(this))
    , m_characterAssignRequestConnection(aDispatcher.sink<PacketEvent<AssignCharacterRequest>>().connect<&CharacterService::OnAssignCharacterRequest>(this))
    , m_transferOwnershipConnection(aDispatcher.sink<PacketEvent<RequestOwnershipTransfer>>().connect<&CharacterService::OnOwnershipTransferRequest>(this))
    , m_ownershipTransferEventConnection(aDispatcher.sink<OwnershipTransferEvent>().connect<&CharacterService::OnOwnershipTransferEvent>(this))
    , m_claimOwnershipConnection(aDispatcher.sink<PacketEvent<RequestOwnershipClaim>>().connect<&CharacterService::OnOwnershipClaimRequest>(this))
    , m_removeCharacterConnection(aDispatcher.sink<CharacterRemoveEvent>().connect<&CharacterService::OnCharacterRemoveEvent>(this))
    , m_characterSpawnedConnection(aDispatcher.sink<CharacterSpawnedEvent>().connect<&CharacterService::OnCharacterSpawned>(this))
    , m_referenceMovementSnapshotConnection(aDispatcher.sink<PacketEvent<ClientReferencesMoveRequest>>().connect<&CharacterService::OnReferencesMoveRequest>(this))
    , m_factionsChangesConnection(aDispatcher.sink<PacketEvent<RequestFactionsChanges>>().connect<&CharacterService::OnFactionsChanges>(this))
    , m_mountConnection(aDispatcher.sink<PacketEvent<MountRequest>>().connect<&CharacterService::OnMountRequest>(this))
    , m_newPackageConnection(aDispatcher.sink<PacketEvent<NewPackageRequest>>().connect<&CharacterService::OnNewPackageRequest>(this))
    , m_requestRespawnConnection(aDispatcher.sink<PacketEvent<RequestRespawn>>().connect<&CharacterService::OnRequestRespawn>(this))
    , m_dialogueConnection(aDispatcher.sink<PacketEvent<DialogueRequest>>().connect<&CharacterService::OnDialogueRequest>(this))
    , m_subtitleConnection(aDispatcher.sink<PacketEvent<SubtitleRequest>>().connect<&CharacterService::OnSubtitleRequest>(this))
{
}

bool CharacterService::BeginOwnerRespawnLifecycle(
    const entt::entity aEntity, Player* apOwner, const std::uint32_t aOwnershipEpoch) noexcept
{
    if (!m_world.valid(aEntity) || !m_world.all_of<CharacterComponent>(aEntity))
        return false;

    const auto* const pOwner = m_world.try_get<OwnerComponent>(aEntity);
    if (!pOwner || !pOwner->IsCurrentOwner(apOwner, aOwnershipEpoch))
        return false;

    auto* const pLifecycle = m_world.try_get<ActorLifecycleComponent>(aEntity);
    const bool startedLifecycle = pLifecycle == nullptr;
    auto* const pCurrentLifecycle = pLifecycle ? pLifecycle : &m_world.emplace<ActorLifecycleComponent>(aEntity);
    if (!pCurrentLifecycle->IsValid() || (!startedLifecycle && !pCurrentLifecycle->TryStartNewIncarnation()))
    {
        if (startedLifecycle)
            m_world.remove<ActorLifecycleComponent>(aEntity);
        spdlog::warn("Cannot respawn actor {:X}: lifecycle generation is unavailable", World::ToInteger(aEntity));
        return false;
    }

    auto& character = m_world.get<CharacterComponent>(aEntity);
    if (CorpseRetentionPolicy::ShouldClearForRespawn(
            m_world.all_of<CorpseRetentionComponent>(aEntity), character.IsDead()))
    {
        // An owner-authorized respawn starts a new incarnation, so the old
        // corpse marker and its expiry must not reject the new alive state.
        m_world.remove<CorpseRetentionComponent>(aEntity);
        spdlog::info("[CorpseRetention] cleared retained corpse marker for respawned actor {:X}", World::ToInteger(aEntity));
    }

    if (auto* const pAnimationComponent = m_world.try_get<AnimationComponent>(aEntity))
        FurnitureUsePolicy::ClearReservation(
            pAnimationComponent->FurnitureUseTargetId,
            pAnimationComponent->RejectedFurnitureTargetId,
            pAnimationComponent->HasEnteredFurniture,
            pAnimationComponent->RejectedFurnitureSawActiveState);

    m_world.GetDispatcher().trigger(ActorRespawnedEvent{
        World::ToInteger(aEntity),
        pCurrentLifecycle->GetGeneration()});
    return true;
}

void CharacterService::Serialize(World& aRegistry, entt::entity aEntity, CharacterSpawnRequest* apSpawnRequest) noexcept
{
    const auto& characterComponent = aRegistry.get<CharacterComponent>(aEntity);

    apSpawnRequest->ServerId = World::ToInteger(aEntity);
    apSpawnRequest->AppearanceBuffer = characterComponent.SaveBuffer;
    apSpawnRequest->ChangeFlags = characterComponent.ChangeFlags;
    apSpawnRequest->FaceTints = characterComponent.FaceTints;
    apSpawnRequest->FactionsContent = characterComponent.FactionsContent;
    apSpawnRequest->IsDead = characterComponent.IsDead();
    apSpawnRequest->IsPlayer = characterComponent.IsPlayer();
    apSpawnRequest->IsWeaponDrawn = characterComponent.IsWeaponDrawn();
    apSpawnRequest->IsPlayerSummon = characterComponent.IsPlayerSummon();
    apSpawnRequest->PlayerId = characterComponent.PlayerId;

    const auto* pOwnerComponent = aRegistry.try_get<OwnerComponent>(aEntity);
    if (pOwnerComponent)
    {
        apSpawnRequest->OwnershipEpoch = pOwnerComponent->OwnershipEpoch;
    }

    const auto* pFormIdComponent = aRegistry.try_get<FormIdComponent>(aEntity);
    if (pFormIdComponent)
    {
        apSpawnRequest->FormId = pFormIdComponent->Id;
    }

    const auto* pInventoryComponent = aRegistry.try_get<InventoryComponent>(aEntity);
    if (pInventoryComponent)
    {
        apSpawnRequest->InventoryContent = pInventoryComponent->Content;
    }

    const auto* pActorValuesComponent = aRegistry.try_get<ActorValuesComponent>(aEntity);
    if (pActorValuesComponent)
    {
        apSpawnRequest->InitialActorValues = pActorValuesComponent->CurrentActorValues;
    }

    if (characterComponent.BaseId)
    {
        apSpawnRequest->BaseId = characterComponent.BaseId.Id;
    }

    if (characterComponent.LeveledNpcPickId)
    {
        apSpawnRequest->LeveledNpcPickId = characterComponent.LeveledNpcPickId.Id;
    }

    const auto* pMovementComponent = aRegistry.try_get<MovementComponent>(aEntity);
    if (pMovementComponent)
    {
        apSpawnRequest->Position = pMovementComponent->Position;
        apSpawnRequest->Rotation.x = pMovementComponent->Rotation.x;
        apSpawnRequest->Rotation.y = pMovementComponent->Rotation.z;
    }

    const auto* pCellIdComponent = aRegistry.try_get<CellIdComponent>(aEntity);
    if (pCellIdComponent)
    {
        apSpawnRequest->CellId = pCellIdComponent->Cell;
    }

    auto& animationComponent = aRegistry.get<AnimationComponent>(aEntity);
    apSpawnRequest->ActionsToReplay = animationComponent.ActionsReplayCache.FormRefinedReplayChain();
}

void CharacterService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (std::isfinite(acEvent.Delta) && acEvent.Delta > 0.f)
    {
        m_tickAccumulator += acEvent.Delta;
        while (m_tickAccumulator >= 1.0)
        {
            m_tickAccumulator -= 1.0;
            ++m_tick;
        }

        ExpireRetainedCorpses();
    }

    ProcessFactionsChanges();
    ProcessMovementChanges();
}

void CharacterService::OnAcceptedCanonicalCreatureDeath(const AcceptedCanonicalCreatureDeathEvent& acEvent) noexcept
{
    const auto entity = static_cast<entt::entity>(acEvent.TargetServerId);
    if (!m_world.valid(entity))
        return;

    const auto* const pCharacter = m_world.try_get<CharacterComponent>(entity);
    const auto* const pIdentity = m_world.try_get<ActorPopulationIdentityComponent>(entity);
    const auto* const pLifecycle = m_world.try_get<ActorLifecycleComponent>(entity);
    if (!pCharacter || !pCharacter->IsDead() || !pIdentity || !pIdentity->IsTrustedCreature() ||
        !pLifecycle || !pLifecycle->IsValid() || pLifecycle->GetGeneration() != acEvent.TargetLifecycleGeneration ||
        m_world.all_of<CorpseRetentionComponent>(entity))
        return;

    const auto expiresAtTick = CorpseRetentionPolicy::ExpirationTick(m_tick, m_creatureCorpseLifetimeSeconds);
    m_world.emplace_or_replace<CorpseRetentionComponent>(entity, expiresAtTick);
    spdlog::info(
        "[CorpseRetention] accepted creature corpse actor {:X} lifecycle {} expires at tick {} (lifetime {}s)",
        acEvent.TargetServerId, acEvent.TargetLifecycleGeneration, expiresAtTick, m_creatureCorpseLifetimeSeconds);
}

void CharacterService::ExpireRetainedCorpses() noexcept
{
    auto view = m_world.view<CharacterComponent, CorpseRetentionComponent>();
    Vector<std::uint32_t> expired;
    for (const auto entity : view)
    {
        auto& corpse = view.get<CorpseRetentionComponent>(entity);
        if (corpse.RemovalQueued)
            continue;

        const auto& character = view.get<CharacterComponent>(entity);
        if (!CorpseRetentionPolicy::IsExpired(true, character.IsDead(), corpse.ExpiresAtTick, m_tick))
            continue;

        corpse.RemovalQueued = true;
        expired.push_back(World::ToInteger(entity));
    }

    for (const auto serverId : expired)
    {
        m_world.GetDispatcher().trigger(CharacterRemoveEvent(serverId));
        spdlog::info("[CorpseRetention] expired creature corpse actor {:X} tick {}", serverId, m_tick);
    }
}

void CharacterService::OnCharacterExteriorCellChange(const CharacterExteriorCellChangeEvent& acEvent) const noexcept
{
    CharacterSpawnRequest spawnMessage;
    Serialize(m_world, acEvent.Entity, &spawnMessage);

    NotifyRemoveCharacter removeMessage;
    removeMessage.ServerId = World::ToInteger(acEvent.Entity);

    for (auto pPlayer : m_world.GetPlayerManager())
    {
        if (acEvent.Owner == pPlayer)
            continue;

        if (pPlayer->GetCellComponent().WorldSpaceId != acEvent.WorldSpaceId || pPlayer->GetCellComponent().WorldSpaceId == acEvent.WorldSpaceId && !GridCellCoords::IsCellInGridCell(acEvent.CurrentCoords, pPlayer->GetCellComponent().CenterCoords, false))
        {
            pPlayer->Send(removeMessage);
        }
        else if (pPlayer->GetCellComponent().WorldSpaceId == acEvent.WorldSpaceId && GridCellCoords::IsCellInGridCell(acEvent.CurrentCoords, pPlayer->GetCellComponent().CenterCoords, false))
        {
            pPlayer->Send(spawnMessage);
        }
    }
}

void CharacterService::OnCharacterInteriorCellChange(const CharacterInteriorCellChangeEvent& acEvent) const noexcept
{
    CharacterSpawnRequest spawnMessage;
    Serialize(m_world, acEvent.Entity, &spawnMessage);

    NotifyRemoveCharacter removeMessage;
    removeMessage.ServerId = World::ToInteger(acEvent.Entity);

    for (auto pPlayer : m_world.GetPlayerManager())
    {
        if (acEvent.Owner == pPlayer)
            continue;

        if (acEvent.NewCell == pPlayer->GetCellComponent().Cell)
            pPlayer->Send(spawnMessage);
        else
            pPlayer->Send(removeMessage);
    }
}

void CharacterService::OnAssignCharacterRequest(const PacketEvent<AssignCharacterRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;
    if (!message.IsValid || !FactionAuthorityPolicy::HasValidPayload(message.FactionsContent))
    {
        spdlog::warn("Rejected assignment from player {:X} with a malformed faction payload", acMessage.pPlayer->GetId());
        return;
    }

    const auto& refId = message.ReferenceId;

    const auto isPlayer = (refId.ModId == 0 && refId.BaseId == 0x14);
    const auto& sessionService = m_world.GetSessionService();
    const bool canAssignPlayer = sessionService.CanAssignPlayer(acMessage.pPlayer->GetConnectionId());
    if (isPlayer && !canAssignPlayer)
        return;

    if (!isPlayer && !sessionService.CanProcessGameplay(acMessage.pPlayer->GetConnectionId()))
        return;

    const auto identity = m_world.GetActorPopulationIdentityResolver().Resolve(refId, message.FormId, message.LeveledNpcPickId);
    if (!isPlayer)
    {
        spdlog::debug(
            "Actor population identity for reference {:x}:{:x}: resolved reference {:08x}, NPC {:08x}, race {:08x} '{}', classification {}, source {}, trusted {}",
            refId.ModId,
            refId.BaseId,
            identity.ResolvedReferenceFormId,
            identity.ResolvedNpcFormId,
            identity.Classification.RaceFormId,
            identity.Classification.RaceEditorId.c_str(),
            static_cast<unsigned>(identity.Classification.Class),
            GetActorPopulationIdentitySourceName(identity.Source),
            identity.IsTrusted());

        const auto decision = m_world.GetActorPopulationAssignmentPolicy().Decide(identity);
        if (decision != ActorPopulationAssignmentDecision::kAllow)
        {
            NotifyCharacterAssignmentRejected rejection{};
            rejection.Cookie = message.Cookie;
            rejection.Reason = decision == ActorPopulationAssignmentDecision::kRejectHumanoid ? CharacterAssignmentRejectReason::kPopulationHumanoidDenied : CharacterAssignmentRejectReason::kPopulationUnknownDenied;
            acMessage.pPlayer->Send(rejection);

            spdlog::debug(
                "Rejected actor assignment for player {:x}: reference {:x}:{:x}, NPC {:08x}, race {:08x} '{}', classification {}, source {}, reason {}",
                acMessage.pPlayer->GetId(),
                refId.ModId,
                refId.BaseId,
                identity.ResolvedNpcFormId,
                identity.Classification.RaceFormId,
                identity.Classification.RaceEditorId.c_str(),
                static_cast<unsigned>(identity.Classification.Class),
                GetActorPopulationIdentitySourceName(identity.Source),
                static_cast<unsigned>(rejection.Reason));
            return;
        }

        if (identity.Source == ActorPopulationIdentitySource::kServerPlacedReference && identity.HasClientClaimedIdentity &&
            identity.ClientClaimedNpcFormId != identity.ResolvedNpcFormId)
        {
            spdlog::debug(
                "Ignored client actor base claim {:08x} for server-resolved placed reference {:08x}; server NPC identity is authoritative",
                identity.ClientClaimedNpcFormId,
                identity.ResolvedNpcFormId);
        }
    }

    const auto isCustom = isPlayer || refId.ModId == std::numeric_limits<uint32_t>::max();

    // Check if id is the player
    if (!isCustom)
    {
        // Look for the character
        auto view = m_world.view<FormIdComponent, ActorValuesComponent, CharacterComponent, MovementComponent, CellIdComponent, OwnerComponent, InventoryComponent>();

        const auto itor = std::find_if(
            std::begin(view), std::end(view),
            [view, refId](auto entity)
            {
                const auto& formIdComponent = view.get<FormIdComponent>(entity);

                return formIdComponent.Id == refId;
            });

        if (itor != std::end(view))
        {
            // Actors created before lifecycle tracking was introduced may still
            // be present in a long-lived server. Give them a server-owned
            // incarnation before returning any assignment state, but never
            // replace an existing generation for the current actor.
            if (!m_world.all_of<ActorLifecycleComponent>(*itor))
            {
                auto& lifecycleComponent = m_world.emplace<ActorLifecycleComponent>(*itor);
                if (!lifecycleComponent.IsValid())
                {
                    m_world.remove<ActorLifecycleComponent>(*itor);
                    spdlog::error("Cannot assign actor {:X}: lifecycle generation allocator is exhausted", World::ToInteger(*itor));
                    return;
                }
            }

            spdlog::debug("FormId: {:x}:{:x} is already managed", refId.ModId, refId.BaseId);

            auto& ownerComponent = view.get<OwnerComponent>(*itor);
            auto& characterComponent = view.get<CharacterComponent>(*itor);
            const bool isOwner = ownerComponent.GetOwner() == acMessage.pPlayer;
            const bool transferToLeader = !isOwner && CanClaimOwnership(acMessage.pPlayer, *itor, ownerComponent.OwnershipEpoch, OwnershipTransferReason::LeaderAssignment);

            if (!characterComponent.LeveledNpcPickId && message.LeveledNpcPickId != GameId{})
            {
                characterComponent.LeveledNpcPickId = FormIdComponent(message.LeveledNpcPickId);
                spdlog::debug(
                    "Stored previously unknown leveled NPC pick {:x}:{:x} for FormId {:x}:{:x}",
                    message.LeveledNpcPickId.ModId,
                    message.LeveledNpcPickId.BaseId,
                    refId.ModId,
                    refId.BaseId);
            }

            AssignCharacterResponse response{};
            response.Cookie = message.Cookie;
            response.Owner = isOwner;
            PopulateAssignmentResponse(*itor, response);
            acMessage.pPlayer->Send(response);

            // The assignment response establishes a remote component before the grant arrives.
            if (transferToLeader)
                TransferOwnership(acMessage.pPlayer, *itor, OwnershipTransferReason::LeaderAssignment);

            // Canonical actors created by older code paths may not have the
            // trusted projection yet. Hydrate it once, but never overwrite an
            // existing incarnation's identity from a later client claim.
            if (!m_world.all_of<ActorPopulationIdentityComponent>(*itor))
                m_world.emplace<ActorPopulationIdentityComponent>(*itor, identity);

            return;
        }
    }

    // This entity has no owner create it
    CreateCharacter(acMessage, identity);
}

void CharacterService::OnOwnershipTransferRequest(const PacketEvent<RequestOwnershipTransfer>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;

    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);
    const auto view = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent, MovementComponent>();
    const auto it = view.find(cEntity);
    if (it == view.end())
    {
        spdlog::debug("Ignored ownership release from player {:X} for missing actor {:X}", acMessage.pPlayer->GetId(), message.ServerId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    if (ownerComponent.GetOwner() != acMessage.pPlayer || ownerComponent.OwnershipEpoch != message.OwnershipEpoch)
    {
        const uint32_t ownerId = ownerComponent.GetOwner() ? ownerComponent.GetOwner()->GetId() : 0;
        spdlog::debug(
            "Ignored ownership release from player {:X} for actor {:X}; current owner is {:X} and requested epoch {} does not match {}",
            acMessage.pPlayer->GetId(), message.ServerId, ownerId, message.OwnershipEpoch, ownerComponent.OwnershipEpoch);
        return;
    }

    if (message.Reason != OwnershipReleaseReason::Relinquish && message.Reason != OwnershipReleaseReason::DeclineGrant)
    {
        spdlog::warn("Ignored ownership release with invalid reason from player {:X} for actor {:X}", acMessage.pPlayer->GetId(), message.ServerId);
        return;
    }

    if (message.Reason == OwnershipReleaseReason::Relinquish && (message.WorldSpaceId || message.CellId) &&
        !CellMovementAuthorityPolicy::HasValidReportedLocation(message.WorldSpaceId, message.CellId, message.Position))
    {
        spdlog::warn(
            "Ignored ownership release with malformed location from player {:X} for actor {:X}", acMessage.pPlayer->GetId(), message.ServerId);
        return;
    }

    auto& characterComponent = view.get<CharacterComponent>(*it);
    if (characterComponent.IsPlayerSummon())
    {
        spdlog::info("Removing summon {:X} after player {:X} relinquished ownership", message.ServerId, acMessage.pPlayer->GetId());
        m_world.GetDispatcher().trigger(CharacterRemoveEvent(message.ServerId));
        return;
    }

    if (message.Reason == OwnershipReleaseReason::Relinquish && (message.WorldSpaceId || message.CellId))
    {
        const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(cEntity);
        if (pFormIdComponent)
        {
            NotifyActorTeleport notify{};
            notify.FormId = pFormIdComponent->Id;
            notify.WorldSpaceId = message.WorldSpaceId;
            notify.CellId = message.CellId;
            notify.Position = message.Position;

            GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
        }

        auto& cellIdComponent = view.get<CellIdComponent>(*it);
        cellIdComponent.WorldSpaceId = message.WorldSpaceId;
        cellIdComponent.Cell = message.CellId;
        cellIdComponent.CenterCoords = GridCellCoords::CalculateGridCellCoords(message.Position);

        auto& movementComponent = view.get<MovementComponent>(*it);
        movementComponent.Position = message.Position;
        movementComponent.Sent = true;
    }

    if (auto* const pAnimationComponent = m_world.try_get<AnimationComponent>(cEntity))
        FurnitureUsePolicy::ClearReservation(
            pAnimationComponent->FurnitureUseTargetId,
            pAnimationComponent->RejectedFurnitureTargetId,
            pAnimationComponent->HasEnteredFurniture,
            pAnimationComponent->RejectedFurnitureSawActiveState);

    // A normal release starts a fresh search. A declined grant continues the current
    // search, retaining failed candidates so unloaded clients cannot bounce ownership.
    if (message.Reason == OwnershipReleaseReason::Relinquish)
        ownerComponent.InvalidOwners.clear();

    ownerComponent.InvalidOwners.push_back(acMessage.pPlayer);

    TransferToNextOwner(cEntity, OwnershipTransferReason::Relinquish);
}

void CharacterService::OnOwnershipTransferEvent(const OwnershipTransferEvent& acEvent) const noexcept
{
    if (auto* const pAnimationComponent = m_world.try_get<AnimationComponent>(acEvent.Entity))
        FurnitureUsePolicy::ClearReservation(
            pAnimationComponent->FurnitureUseTargetId,
            pAnimationComponent->RejectedFurnitureTargetId,
            pAnimationComponent->HasEnteredFurniture,
            pAnimationComponent->RejectedFurnitureSawActiveState);

    // A disconnect starts a fresh search; previously unavailable clients may be ready now.
    const auto view = m_world.view<OwnerComponent>();
    if (const auto it = view.find(acEvent.Entity); it != view.end())
        view.get<OwnerComponent>(*it).InvalidOwners.clear();

    TransferToNextOwner(acEvent.Entity, OwnershipTransferReason::OwnerUnavailable);
}

void CharacterService::OnCharacterRemoveEvent(const CharacterRemoveEvent& acEvent) const noexcept
{
    const auto view = m_world.view<OwnerComponent>();
    const auto it = view.find(static_cast<entt::entity>(acEvent.ServerId));
    if (it == view.end())
        return;

    const auto entity = *it;
    NotifyAndDestroyCharacter(
        acEvent.ServerId, m_world.GetPlayerManager(), [entity] { GameServer::Get()->GetWorld().GetScriptService().HandleCharacterDestoy(entity); },
        // Registry destruction removes the lifecycle component with the canonical
        // actor state; its generation is intentionally never recycled.
        [this, entity] { m_world.destroy(entity); });
    spdlog::debug("Character destroyed {:X}", acEvent.ServerId);
}

void CharacterService::OnOwnershipClaimRequest(const PacketEvent<RequestOwnershipClaim>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);

    if (!CanClaimOwnership(acMessage.pPlayer, cEntity, message.ExpectedOwnershipEpoch, OwnershipTransferReason::LeaderClaim))
        return;

    TransferOwnership(acMessage.pPlayer, cEntity, OwnershipTransferReason::LeaderClaim);
}

void CharacterService::OnCharacterSpawned(const CharacterSpawnedEvent& acEvent) const noexcept
{
    CharacterSpawnRequest message;
    Serialize(m_world, acEvent.Entity, &message);

    const auto& ownerComp = m_world.get<OwnerComponent>(acEvent.Entity);
    if (!GameServer::Get()->SendToPlayersInRange(message, acEvent.Entity, ownerComp.GetOwner()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

    GameServer::Get()->GetWorld().GetScriptService().HandleCharacterSpawn(acEvent.Entity);
}

void CharacterService::OnReferencesMoveRequest(const PacketEvent<ClientReferencesMoveRequest>& acMessage) const noexcept
{
    OwnerView<CharacterComponent, AnimationComponent, MovementComponent, CellIdComponent> view(m_world, acMessage.GetSender());

    auto& message = acMessage.Packet;

    for (auto& entry : message.Updates)
    {
        const auto entity = static_cast<entt::entity>(entry.first);

        auto itor = view.find(entity);
        if (itor == std::end(view))
        {
            spdlog::debug("{:x} requested move of {:x} but does not exist", acMessage.pPlayer->GetConnectionId(), World::ToInteger(entity));
            continue;
        }

        auto& update = entry.second;
        auto& ownerComponent = view.get<OwnerComponent>(*itor);
        if (!MovementAuthorityPolicy::IsAuthorized(
                true, true, ownerComponent.IsCurrentOwner(acMessage.pPlayer, update.OwnershipEpoch), update.OwnershipEpoch))
        {
            spdlog::debug(
                "Rejected movement update from player {:X} for actor {:X}; requested epoch {} does not match current epoch {}",
                acMessage.pPlayer->GetId(), entry.first, update.OwnershipEpoch, ownerComponent.OwnershipEpoch);
            continue;
        }

        if (!MovementAuthorityPolicy::HasValidPayload(update))
        {
            spdlog::debug("Rejected malformed movement update from player {:X} for actor {:X}", acMessage.pPlayer->GetId(), entry.first);
            continue;
        }

        auto& movementComponent = view.get<MovementComponent>(*itor);
        auto& cellIdComponent = view.get<CellIdComponent>(*itor);
        auto& animationComponent = view.get<AnimationComponent>(*itor);

        movementComponent.Tick = message.Tick;

        const auto movementCopy = movementComponent;

        auto& movement = update.UpdatedMovement;

        const bool isInFurniture = FurnitureUsePolicy::IsInFurniture(movement.Variables);
        const bool hasFurnitureExitAction = std::any_of(
            update.ActionEvents.begin(), update.ActionEvents.end(), [](const ActionEvent& acAction)
            {
                const std::string_view eventName{acAction.EventName.data(), acAction.EventName.size()};
                return FurnitureUsePolicy::IsFurnitureExitAction(eventName);
            });

        if (animationComponent.RejectedFurnitureTargetId)
        {
            if (hasFurnitureExitAction || (!isInFurniture && animationComponent.RejectedFurnitureSawActiveState))
            {
                FurnitureUsePolicy::ClearRejectedTarget(
                    animationComponent.RejectedFurnitureTargetId,
                    animationComponent.RejectedFurnitureSawActiveState);
            }
            else if (isInFurniture)
            {
                animationComponent.RejectedFurnitureSawActiveState = true;
                continue;
            }
            else
            {
                const float dx = movement.Position.x - movementCopy.Position.x;
                const float dy = movement.Position.y - movementCopy.Position.y;
                const float dz = movement.Position.z - movementCopy.Position.z;
                constexpr float kPositionCorrectionTolerance = 16.f;
                if (dx * dx + dy * dy + dz * dz <= kPositionCorrectionTolerance * kPositionCorrectionTolerance)
                {
                    FurnitureUsePolicy::ClearRejectedTarget(
                        animationComponent.RejectedFurnitureTargetId,
                        animationComponent.RejectedFurnitureSawActiveState);
                }
                else
                {
                    continue;
                }
            }
        }

        bool furnitureUseDenied = false;
        for (const auto& action : update.ActionEvents)
        {
            const std::string_view eventName{action.EventName.data(), action.EventName.size()};
            if (!FurnitureUsePolicy::IsSeatEntryAction(eventName))
                continue;

            if (!action.TargetId)
            {
                spdlog::warn("Seat action '{}' from actor {:X} has no furniture target; occupancy was not reserved",
                             eventName, entry.first);
                continue;
            }

            const auto furnitureView = m_world.view<FormIdComponent, ObjectComponent, CellIdComponent>();
            const auto furnitureIt = std::find_if(
                furnitureView.begin(), furnitureView.end(),
                [furnitureView, targetId = action.TargetId](const entt::entity aEntity)
                {
                    return furnitureView.get<FormIdComponent>(aEntity).Id == targetId &&
                        furnitureView.get<ObjectComponent>(aEntity).IsFurniture;
                });
            const bool isKnownFurniture = furnitureIt != furnitureView.end();
            bool isFurnitureInRange = false;
            if (isKnownFurniture)
            {
                const auto& furnitureCell = furnitureView.get<CellIdComponent>(*furnitureIt);
                const auto& characterComponent = view.get<CharacterComponent>(*itor);
                isFurnitureInRange = ObjectInteractionPolicy::IsInSenderRange(
                    cellIdComponent.Cell, cellIdComponent.WorldSpaceId, cellIdComponent.CenterCoords,
                    furnitureCell.Cell, furnitureCell.WorldSpaceId, furnitureCell.CenterCoords,
                    characterComponent.IsDragon());
            }

            entt::entity occupant = entt::null;
            auto occupiedView = m_world.view<AnimationComponent>();
            for (const auto otherEntity : occupiedView)
            {
                if (otherEntity == entity || occupiedView.get<AnimationComponent>(otherEntity).FurnitureUseTargetId != action.TargetId)
                    continue;

                occupant = otherEntity;
                break;
            }

            if (!FurnitureUsePolicy::CanEnter(
                    action.TargetId, isKnownFurniture, isFurnitureInRange, occupant != entt::null))
            {
                animationComponent.RejectedFurnitureTargetId = action.TargetId;
                animationComponent.RejectedFurnitureSawActiveState = isInFurniture;

                NotifyFurnitureUseDenied denied;
                denied.ActorId = entry.first;
                denied.OwnershipEpoch = update.OwnershipEpoch;
                denied.AuthoritativeMovement.CellId = cellIdComponent.Cell;
                denied.AuthoritativeMovement.WorldSpaceId = cellIdComponent.WorldSpaceId;
                denied.AuthoritativeMovement.Position = movementCopy.Position;
                denied.AuthoritativeMovement.Rotation.x = movementCopy.Rotation.x;
                denied.AuthoritativeMovement.Rotation.y = movementCopy.Rotation.z;
                denied.AuthoritativeMovement.Direction = movementCopy.Direction;
                denied.AuthoritativeMovement.Variables = movementCopy.Variables;
                acMessage.pPlayer->Send(denied);

                spdlog::warn("Denied seat action '{}' for actor {:X} on invalid or occupied furniture {:X}; occupant actor {:X}, epoch {}",
                             eventName, entry.first, action.TargetId.LogFormat(), World::ToInteger(occupant), update.OwnershipEpoch);
                furnitureUseDenied = true;
                break;
            }

            if (animationComponent.FurnitureUseTargetId != action.TargetId)
            {
                animationComponent.FurnitureUseTargetId = action.TargetId;
                animationComponent.HasEnteredFurniture = isInFurniture;
                spdlog::info("Reserved furniture {:X} for actor {:X}, seat action '{}', epoch {}",
                             action.TargetId.LogFormat(), entry.first, eventName, update.OwnershipEpoch);
            }
        }

        if (furnitureUseDenied)
            continue;

        movementComponent.Position = movement.Position;
        movementComponent.Rotation = glm::vec3(movement.Rotation.x, 0.f, movement.Rotation.y);
        movementComponent.Variables = movement.Variables;
        movementComponent.Direction = movement.Direction;

        cellIdComponent.Cell = movement.CellId;
        cellIdComponent.WorldSpaceId = movement.WorldSpaceId;
        cellIdComponent.CenterCoords = GridCellCoords::CalculateGridCellCoords(movement.Position.x, movement.Position.y);

        for (auto& action : update.ActionEvents)
        {
            auto [canceled, reason] = GameServer::Get()->GetWorld().GetScriptService().HandleCharacterMove(entity);
            if (canceled)
            {
                const std::string_view eventName{action.EventName.data(), action.EventName.size()};
                if (FurnitureUsePolicy::IsSeatEntryAction(eventName) && !animationComponent.HasEnteredFurniture &&
                    animationComponent.FurnitureUseTargetId == action.TargetId)
                    animationComponent.FurnitureUseTargetId = {};
                continue;
            }

            const std::string_view eventName{action.EventName.data(), action.EventName.size()};
            if (FurnitureUsePolicy::IsFurnitureExitAction(eventName) && animationComponent.FurnitureUseTargetId)
            {
                spdlog::info("Furniture use released by actor {:X}, furniture {:X}, event '{}'",
                             entry.first, animationComponent.FurnitureUseTargetId.LogFormat(), eventName);
                animationComponent.FurnitureUseTargetId = {};
                animationComponent.HasEnteredFurniture = false;
            }

            animationComponent.CurrentAction = action;

            ActionEvent replayAction = animationComponent.CurrentAction;
            if (FurnitureUsePolicy::IsSeatEntryAction(eventName))
                replayAction = ActionReplayCache::NormalizeForImmediateReplay(replayAction);
            animationComponent.Actions.push_back(std::move(replayAction));
        }

        if (isInFurniture)
            animationComponent.HasEnteredFurniture = static_cast<bool>(animationComponent.FurnitureUseTargetId);
        else if (animationComponent.HasEnteredFurniture)
        {
            spdlog::info("Furniture use ended for actor {:X}, furniture {:X}",
                         entry.first, animationComponent.FurnitureUseTargetId.LogFormat());
            animationComponent.FurnitureUseTargetId = {};
            animationComponent.HasEnteredFurniture = false;
        }

        animationComponent.ActionsReplayCache.AppendAll(update.ActionEvents);

        movementComponent.Sent = false;
    }
}

void CharacterService::OnFactionsChanges(const PacketEvent<RequestFactionsChanges>& acMessage) const noexcept
{
    auto view = m_world.view<OwnerComponent, CharacterComponent>();

    auto& message = acMessage.Packet;

    for (auto& [id, update] : message.Changes)
    {
        const auto entity = static_cast<entt::entity>(id);
        auto it = view.find(entity);

        if (it == std::end(view))
            continue;

        const auto& ownerComponent = view.get<OwnerComponent>(*it);
        const bool isPersistentCharacter = m_world.all_of<PersistentCharacterComponent>(*it);
        if (!FactionAuthorityPolicy::IsAuthorized(
                true,
                true,
                ownerComponent.GetOwner() == acMessage.pPlayer,
                isPersistentCharacter,
                ownerComponent.OwnershipEpoch,
                update.OwnershipEpoch))
        {
            spdlog::debug(
                "Rejected faction update from player {:X} for actor {:X}; requested epoch {} does not match current epoch {}",
                acMessage.pPlayer->GetId(), id, update.OwnershipEpoch, ownerComponent.OwnershipEpoch);
            continue;
        }

        if (!FactionAuthorityPolicy::HasValidPayload(update.FactionsContent))
        {
            spdlog::debug("Rejected malformed faction update from player {:X} for actor {:X}", acMessage.pPlayer->GetId(), id);
            continue;
        }

        auto& characterComponent = view.get<CharacterComponent>(*it);
        characterComponent.FactionsContent = update.FactionsContent;
        characterComponent.SetDirtyFactions(true);
    }
}

void CharacterService::OnMountRequest(const PacketEvent<MountRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    const entt::entity cRiderEntity = static_cast<entt::entity>(message.RiderId);
    const entt::entity cMountEntity = static_cast<entt::entity>(message.MountId);
    const auto view = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent>();
    const auto riderIt = view.find(cRiderEntity);
    const auto mountIt = view.find(cMountEntity);

    if (riderIt == view.end() || mountIt == view.end() || cRiderEntity == cMountEntity)
    {
        spdlog::debug("Rejected mount request from player {:X} because rider {:X} or mount {:X} is invalid", acMessage.pPlayer->GetId(), message.RiderId, message.MountId);
        return;
    }

    if (!view.get<CharacterComponent>(*mountIt).IsMount())
    {
        spdlog::warn("Rejected mount request from player {:X} because actor {:X} is not a mount", acMessage.pPlayer->GetId(), message.MountId);
        return;
    }

    const auto& riderOwner = view.get<OwnerComponent>(*riderIt);
    const auto& mountOwner = view.get<OwnerComponent>(*mountIt);
    if (riderOwner.GetOwner() != acMessage.pPlayer || riderOwner.OwnershipEpoch != message.RiderOwnershipEpoch || mountOwner.OwnershipEpoch != message.MountOwnershipEpoch)
    {
        spdlog::debug(
            "Rejected stale mount request from player {:X} for rider {:X} at epoch {} and mount {:X} at epoch {}; current epochs are {} and {}",
            acMessage.pPlayer->GetId(), message.RiderId, message.RiderOwnershipEpoch, message.MountId, message.MountOwnershipEpoch,
            riderOwner.OwnershipEpoch, mountOwner.OwnershipEpoch);
        return;
    }

    const auto& mountCell = view.get<CellIdComponent>(*mountIt);
    if (!acMessage.pPlayer->GetCellComponent().IsInRange(mountCell, view.get<CharacterComponent>(*mountIt).IsDragon()))
    {
        spdlog::debug("Rejected mount request from player {:X} because mount {:X} is out of range", acMessage.pPlayer->GetId(), message.MountId);
        return;
    }

    if (!TransferOwnership(acMessage.pPlayer, *mountIt, OwnershipTransferReason::Mount))
        return;

    NotifyMount notify;
    notify.RiderId = message.RiderId;
    notify.MountId = message.MountId;

    if (!GameServer::Get()->SendToPlayersInRange(notify, cMountEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void CharacterService::OnNewPackageRequest(const PacketEvent<NewPackageRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    const auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto it = characterView.find(static_cast<entt::entity>(message.ActorId));
    if (it == characterView.end() || !characterView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
        return;

    NotifyNewPackage notify;
    notify.ActorId = message.ActorId;
    notify.PackageId = message.PackageId;
    notify.OwnershipEpoch = message.OwnershipEpoch;

    const entt::entity cEntity = static_cast<entt::entity>(message.ActorId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void CharacterService::OnRequestRespawn(const PacketEvent<RequestRespawn>& acMessage) noexcept
{
    auto view = m_world.view<OwnerComponent, CharacterComponent>();
    auto it = view.find(static_cast<entt::entity>(acMessage.Packet.ActorId));
    if (it == view.end())
    {
        spdlog::warn("No OwnerComponent found for actor id {:X}", acMessage.Packet.ActorId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    if (acMessage.Packet.OwnershipEpoch == 0 || ownerComponent.OwnershipEpoch != acMessage.Packet.OwnershipEpoch)
        return;

    if (ownerComponent.IsCurrentOwner(acMessage.pPlayer, acMessage.Packet.OwnershipEpoch))
    {
        if (!BeginOwnerRespawnLifecycle(*it, acMessage.pPlayer, acMessage.Packet.OwnershipEpoch))
            return;

        // Replay cache needs to be cleared when the current owner respawns.
        if (auto* pAnimationComponent = m_world.try_get<AnimationComponent>(*it))
            pAnimationComponent->ActionsReplayCache.Clear();

        if (!acMessage.Packet.AppearanceBuffer.empty())
        {
            auto& characterComponent = view.get<CharacterComponent>(*it);
            characterComponent.SaveBuffer = acMessage.Packet.AppearanceBuffer;
            characterComponent.ChangeFlags = acMessage.Packet.ChangeFlags;
        }

        NotifyRespawn notify;
        notify.ActorId = acMessage.Packet.ActorId;
        notify.OwnershipEpoch = ownerComponent.OwnershipEpoch;

        if (!GameServer::Get()->SendToPlayersInRange(notify, *it, acMessage.GetSender()))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    }
    else
    {
        CharacterSpawnRequest message;
        Serialize(m_world, *it, &message);

        acMessage.GetSender()->Send(message);
    }
}

void CharacterService::OnDialogueRequest(const PacketEvent<DialogueRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    if (!CanRelayNpcPresentation(m_world, *acMessage.pPlayer, message.ServerId))
        return;

    NotifyDialogue notify{};
    notify.ServerId = message.ServerId;
    notify.SoundFilename = message.SoundFilename;

    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void CharacterService::OnSubtitleRequest(const PacketEvent<SubtitleRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    if (!CanRelayNpcPresentation(m_world, *acMessage.pPlayer, message.ServerId))
        return;

    NotifySubtitle notify{};
    notify.ServerId = message.ServerId;
    notify.Text = message.Text;

    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void CharacterService::CreateCharacter(const PacketEvent<AssignCharacterRequest>& acMessage, const ActorPopulationIdentity& acIdentity) const noexcept
{
    auto& message = acMessage.Packet;

    const auto gameId = message.ReferenceId;
    const auto baseId = message.FormId;

    const auto isTemporary = gameId.ModId == std::numeric_limits<uint32_t>::max();
    const auto isPlayer = (gameId.ModId == 0 && gameId.BaseId == 0x14);
    const auto isCustom = isPlayer || isTemporary;
    const bool isPersistentPlayerAssignment = isPlayer && m_world.GetSessionService().CanAssignPlayer(acMessage.pPlayer->GetConnectionId());

    std::optional<Persistence::CharacterRecord> persistentCharacter;
    if (isPersistentPlayerAssignment)
    {
        if (acMessage.pPlayer->GetCharacter().has_value())
        {
            spdlog::warn("Rejected duplicate local player assignment for connection {:x}", acMessage.pPlayer->GetConnectionId());
            return;
        }

        persistentCharacter = m_world.GetSessionService().GetSelectedCharacterForAssignment(acMessage.pPlayer->GetConnectionId());
        if (!persistentCharacter.has_value())
        {
            NotifyCharacterReadyResult failure{};
            failure.Status = CharacterReadyStatus::kCharacterMismatchOrUnavailable;
            acMessage.pPlayer->Send(failure);
            spdlog::error("Persistent character disappeared or became invalid before assignment for connection {:x}", acMessage.pPlayer->GetConnectionId());
            return;
        }
    }

    // Reject malformed player references before allocating an ECS entity.
    if (isCustom && baseId != GameId{} && !isTemporary)
    {
        spdlog::warn("Unexpected NpcId, player {:x} might be forging packets", acMessage.pPlayer->GetConnectionId());
        return;
    }

    const auto cEntity = m_world.create();
    auto& lifecycleComponent = m_world.emplace<ActorLifecycleComponent>(cEntity);
    if (!lifecycleComponent.IsValid())
    {
        m_world.destroy(cEntity);
        spdlog::error("Cannot create actor: lifecycle generation allocator is exhausted");
        return;
    }

    // For player characters and temporary forms
    if (!isCustom)
        m_world.emplace<FormIdComponent>(cEntity, gameId.BaseId, gameId.ModId);

    auto* const pServer = GameServer::Get();

    m_world.emplace<OwnerComponent>(cEntity, acMessage.pPlayer);
    m_world.emplace<ActorPopulationIdentityComponent>(cEntity, acIdentity);

    const GameId cellId = persistentCharacter.has_value() ? persistentCharacter->Cell : message.CellId;
    const GameId worldSpaceId = persistentCharacter.has_value() ? persistentCharacter->WorldSpace : message.WorldSpaceId;
    const auto position = persistentCharacter.has_value() ? glm::vec3{persistentCharacter->PositionX, persistentCharacter->PositionY, persistentCharacter->PositionZ} : message.Position;

    auto& cellIdComponent = m_world.emplace<CellIdComponent>(cEntity, cellId);
    if (worldSpaceId != GameId{})
    {
        cellIdComponent.WorldSpaceId = worldSpaceId;
        cellIdComponent.CenterCoords = GridCellCoords::CalculateGridCellCoords(position.x, position.y);
    }

    auto& characterComponent = m_world.emplace<CharacterComponent>(cEntity);
    characterComponent.ChangeFlags = message.ChangeFlags;
    characterComponent.SaveBuffer = std::move(message.AppearanceBuffer);
    characterComponent.BaseId = FormIdComponent(message.FormId);
    // Client-authoritative like BaseId; worst case a forged id changes which NPC identity renders.
    if (message.LeveledNpcPickId != GameId{})
        characterComponent.LeveledNpcPickId = FormIdComponent(message.LeveledNpcPickId);

    if (characterComponent.LeveledNpcPickId)
        spdlog::debug("Stored leveled NPC pick {:x}:{:x} for FormId {:x}:{:x}", message.LeveledNpcPickId.ModId, message.LeveledNpcPickId.BaseId, gameId.ModId, gameId.BaseId);
    characterComponent.FaceTints = message.FaceTints;
    characterComponent.FactionsContent = message.FactionsContent;
    characterComponent.SetDead(message.CurrentActorData.IsDead);
    characterComponent.SetPlayer(isPlayer);
    characterComponent.SetWeaponDrawn(message.CurrentActorData.IsWeaponDrawn);
    characterComponent.SetDragon(message.IsDragon);
    characterComponent.SetMount(message.IsMount);
    characterComponent.SetPlayerSummon(message.IsPlayerSummon);

    auto& inventoryComponent = m_world.emplace<InventoryComponent>(cEntity);
    inventoryComponent.Content = message.CurrentActorData.InitialInventory;

    auto& actorValuesComponent = m_world.emplace<ActorValuesComponent>(cEntity);
    actorValuesComponent.CurrentActorValues = message.CurrentActorData.InitialActorValues;
    if (persistentCharacter.has_value())
    {
        actorValuesComponent.CurrentActorValues.ActorValuesList[kHealthActorValue] = persistentCharacter->Health;
        actorValuesComponent.CurrentActorValues.ActorValuesList[kMagickaActorValue] = persistentCharacter->Magicka;
        actorValuesComponent.CurrentActorValues.ActorValuesList[kStaminaActorValue] = persistentCharacter->Stamina;

        auto& persistentComponent = m_world.emplace<PersistentCharacterComponent>(cEntity);
        persistentComponent.CharacterId = persistentCharacter->Id;
        // This owner was loaded through the owner-scoped session lookup; never copy it from
        // client assignment data. The component is server-only and is not serialized.
        persistentComponent.OwnerProfileId = persistentCharacter->OwnerProfileId;
    }

    spdlog::debug("FormId: {:x}:{:x} - NpcId: {:x}:{:x} assigned to {:x}", gameId.ModId, gameId.BaseId, baseId.ModId, baseId.BaseId, acMessage.pPlayer->GetConnectionId());

    auto& movementComponent = m_world.emplace<MovementComponent>(cEntity);
    movementComponent.Tick = pServer->GetTick();
    movementComponent.Position = position;
    movementComponent.Rotation = {message.Rotation.x, 0.f, message.Rotation.y};
    movementComponent.Sent = false;

    m_world.emplace<AnimationComponent>(cEntity);

    // If this is a player character store a ref and trigger an event
    if (isPlayer)
    {
        const auto pPlayer = acMessage.pPlayer;

        // The character link is needed while completing the assignment, but persisted
        // player-facing metadata is committed only after the session transition succeeds.
        pPlayer->SetCharacter(cEntity);
        characterComponent.PlayerId = pPlayer->GetId();

        auto& dispatcher = m_world.GetDispatcher();
        if (persistentCharacter.has_value())
        {
            if (!m_world.GetSessionService().CompletePlayerAssignment(pPlayer->GetConnectionId(), persistentCharacter->Id))
            {
                pPlayer->ClearCharacter();
                m_world.destroy(cEntity);
                NotifyCharacterReadyResult failure{};
                failure.Status = CharacterReadyStatus::kCharacterMismatchOrUnavailable;
                pPlayer->Send(failure);
                spdlog::error("Failed to complete persistent player assignment for connection {:x}", pPlayer->GetConnectionId());
                return;
            }
        }

        if (persistentCharacter.has_value())
        {
            pPlayer->SetUsername(String(persistentCharacter->Name.c_str()));
            pPlayer->SetLevel(static_cast<std::uint16_t>(persistentCharacter->Level));
            pPlayer->SetCellComponent(cellIdComponent);
        }

        pPlayer->GetQuestLogComponent().QuestContent = message.QuestContent;

        dispatcher.trigger(PlayerEnterWorldEvent(pPlayer));
    }

    AssignCharacterResponse response{};
    response.Cookie = message.Cookie;
    response.Owner = true;
    PopulateAssignmentResponse(cEntity, response);

    pServer->Send(acMessage.pPlayer->GetConnectionId(), response);

    if (persistentCharacter.has_value())
    {
        NotifyCharacterEnteredWorld enteredWorld{};
        enteredWorld.CharacterId = static_cast<std::uint64_t>(persistentCharacter->Id);
        pServer->Send(acMessage.pPlayer->GetConnectionId(), enteredWorld);
    }

    auto& dispatcher = m_world.GetDispatcher();
    dispatcher.trigger(CharacterSpawnedEvent(cEntity));
}

void CharacterService::PopulateAssignmentResponse(const entt::entity aEntity, AssignCharacterResponse& aResponse) const noexcept
{
    aResponse.ServerId = World::ToInteger(aEntity);

    if (const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(aEntity))
        aResponse.OwnershipEpoch = pOwnerComponent->OwnershipEpoch;

    if (const auto* pActorValuesComponent = m_world.try_get<ActorValuesComponent>(aEntity))
        aResponse.AllActorValues = pActorValuesComponent->CurrentActorValues;

    if (const auto* pInventoryComponent = m_world.try_get<InventoryComponent>(aEntity))
        aResponse.CurrentInventory = pInventoryComponent->Content;

    if (const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(aEntity))
    {
        aResponse.PlayerId = pCharacterComponent->PlayerId;
        aResponse.IsDead = pCharacterComponent->IsDead();
        aResponse.IsWeaponDrawn = pCharacterComponent->IsWeaponDrawn();
        aResponse.LeveledNpcPickId = pCharacterComponent->LeveledNpcPickId.Id;

        if (pCharacterComponent->LeveledNpcPickId)
        {
            spdlog::debug(
                "Including leveled NPC pick in assignment response for actor {:X}, pick: {:x}:{:x}, owner: {}, epoch: {}",
                aResponse.ServerId,
                aResponse.LeveledNpcPickId.ModId,
                aResponse.LeveledNpcPickId.BaseId,
                aResponse.Owner,
                aResponse.OwnershipEpoch);
        }
    }

    if (const auto* pMovementComponent = m_world.try_get<MovementComponent>(aEntity))
        aResponse.Position = pMovementComponent->Position;

    if (const auto* pCellIdComponent = m_world.try_get<CellIdComponent>(aEntity))
    {
        aResponse.CellId = pCellIdComponent->Cell;
        aResponse.WorldSpaceId = pCellIdComponent->WorldSpaceId;
    }

    if (auto* pAnimationComponent = m_world.try_get<AnimationComponent>(aEntity))
        aResponse.ActionsToReplay = pAnimationComponent->ActionsReplayCache.FormRefinedReplayChain();
}

const char* CharacterService::GetOwnershipTransferReasonName(const OwnershipTransferReason aReason) noexcept
{
    switch (aReason)
    {
    case OwnershipTransferReason::LeaderAssignment:
        return "party leader assignment";
    case OwnershipTransferReason::LeaderClaim:
        return "party leader claim";
    case OwnershipTransferReason::Mount:
        return "mounting";
    case OwnershipTransferReason::Relinquish:
        return "owner relinquished control";
    case OwnershipTransferReason::OwnerUnavailable:
        return "owner became unavailable";
    }

    return "unknown reason";
}

bool CharacterService::CanClaimOwnership(Player* apPlayer, const entt::entity aEntity, const uint32_t aExpectedOwnershipEpoch, const OwnershipTransferReason aReason) const noexcept
{
    const uint32_t serverId = World::ToInteger(aEntity);
    const char* pReasonName = GetOwnershipTransferReasonName(aReason);
    const auto view = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent, FormIdComponent>();
    const auto it = view.find(aEntity);
    if (it == view.end())
    {
        spdlog::debug("Rejected {} from player {:X} because actor {:X} is unavailable or temporary", pReasonName, apPlayer->GetId(), serverId);
        return false;
    }

    const auto& ownerComponent = view.get<OwnerComponent>(*it);
    const auto& characterComponent = view.get<CharacterComponent>(*it);
    const auto& cellIdComponent = view.get<CellIdComponent>(*it);
    Player* const pCurrentOwner = ownerComponent.GetOwner();
    const uint32_t currentOwnerId = pCurrentOwner ? pCurrentOwner->GetId() : 0;

    const auto reject = [&](const char* apReason)
    {
        spdlog::debug(
            "Rejected {} from player {:X} for actor {:X}: {} (requested epoch {}, current owner {:X}, current epoch {})",
            pReasonName, apPlayer->GetId(), serverId, apReason, aExpectedOwnershipEpoch, currentOwnerId, ownerComponent.OwnershipEpoch);
        return false;
    };

    if (aExpectedOwnershipEpoch == 0 || ownerComponent.OwnershipEpoch != aExpectedOwnershipEpoch)
        return reject("the ownership epoch is stale");

    if (!pCurrentOwner || pCurrentOwner == apPlayer)
        return reject("the player already owns the actor");

    if (characterComponent.IsMount() || characterComponent.IsPlayer())
        return reject("the actor cannot be claimed");

    if (!apPlayer->GetCellComponent().IsInRange(cellIdComponent, characterComponent.IsDragon()))
        return reject("the actor is out of range");

    if (!m_world.GetAuthorityService().CanClaimActor(apPlayer, pCurrentOwner))
        return reject("the player is not eligible to claim actor authority");

    return true;
}

bool CharacterService::TransferOwnership(Player* apPlayer, const entt::entity aEntity, const OwnershipTransferReason aReason, const bool aResetInvalidOwners) const noexcept
{
    const char* pReasonName = GetOwnershipTransferReasonName(aReason);
    const auto view = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent>();
    const auto it = view.find(aEntity);
    if (!apPlayer || it == view.end())
    {
        spdlog::warn("Cannot transfer ownership of actor {:X} for {} because the target is invalid", World::ToInteger(aEntity), pReasonName);
        return false;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    Player* const pOldOwner = ownerComponent.GetOwner();
    if (pOldOwner == apPlayer)
        return true;

    if (auto* const pAnimationComponent = m_world.try_get<AnimationComponent>(aEntity))
        FurnitureUsePolicy::ClearReservation(
            pAnimationComponent->FurnitureUseTargetId,
            pAnimationComponent->RejectedFurnitureTargetId,
            pAnimationComponent->HasEnteredFurniture,
            pAnimationComponent->RejectedFurnitureSawActiveState);

    const uint32_t oldOwnerId = pOldOwner ? pOldOwner->GetId() : 0;
    const uint32_t oldEpoch = ownerComponent.OwnershipEpoch;
    uint32_t newEpoch = oldEpoch + 1;
    if (newEpoch == 0)
        newEpoch = 1;

    NotifyOwnershipTransfer notify{};
    notify.ServerId = World::ToInteger(aEntity);
    notify.OwnerPlayerId = apPlayer->GetId();
    notify.OwnershipEpoch = newEpoch;
    notify.CurrentActorData = BuildActorData(aEntity);
    notify.LeveledNpcPickId = view.get<CharacterComponent>(*it).LeveledNpcPickId.Id;

    ownerComponent.SetOwner(apPlayer);
    ownerComponent.OwnershipEpoch = newEpoch;
    if (aResetInvalidOwners)
        ownerComponent.InvalidOwners.clear();

    if (!GameServer::Get()->SendToPlayersInRange(notify, aEntity, pOldOwner))
        spdlog::error("Failed to broadcast ownership transfer for actor {:X}", notify.ServerId);

    // The former owner may already be out of range, so notify it directly as well.
    if (pOldOwner)
        pOldOwner->Send(notify);

    spdlog::info(
        "Transferred ownership of actor {:X} from player {:X} to player {:X} for {} (epoch {} to {})",
        notify.ServerId, oldOwnerId, notify.OwnerPlayerId, pReasonName, oldEpoch, newEpoch);

    return true;
}

void CharacterService::TransferToNextOwner(const entt::entity aEntity, const OwnershipTransferReason aReason) const noexcept
{
    const char* pReasonName = GetOwnershipTransferReasonName(aReason);
    const auto view = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent>();
    const auto it = view.find(aEntity);
    if (it == view.end())
    {
        spdlog::warn("Cannot select a new owner for actor {:X} after {} because the actor is missing", World::ToInteger(aEntity), pReasonName);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    const auto& characterComponent = view.get<CharacterComponent>(*it);
    const auto& cellIdComponent = view.get<CellIdComponent>(*it);

    // A canonical corpse has no simulation owner. Keep its server entity and
    // invalidate the departing owner's epoch instead of handing it off or
    // destroying it when no one else is nearby.
    if (MakeRetainedCorpseOwnerless(aEntity, aReason))
        return;

    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer == ownerComponent.GetOwner())
            continue;

        if (std::find(ownerComponent.InvalidOwners.begin(), ownerComponent.InvalidOwners.end(), pPlayer) != ownerComponent.InvalidOwners.end())
            continue;

        if (!pPlayer->GetCellComponent().IsInRange(cellIdComponent, characterComponent.IsDragon()))
            continue;

        // Retain every owner that declined this handoff chain so the actor cannot bounce between unloaded clients.
        if (TransferOwnership(pPlayer, aEntity, aReason, false))
            return;
    }

    spdlog::info("Removing actor {:X} after {} because no eligible owner remains", World::ToInteger(aEntity), pReasonName);
    m_world.GetDispatcher().trigger(CharacterRemoveEvent(World::ToInteger(aEntity)));
}

bool CharacterService::MakeRetainedCorpseOwnerless(const entt::entity aEntity, const OwnershipTransferReason aReason) const noexcept
{
    const auto view = m_world.view<OwnerComponent, CharacterComponent, CorpseRetentionComponent>();
    const auto it = view.find(aEntity);
    if (it == view.end() || !view.get<CharacterComponent>(*it).IsDead())
        return false;

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    Player* const pOldOwner = ownerComponent.GetOwner();
    if (!pOldOwner)
        return true;

    const auto oldEpoch = ownerComponent.OwnershipEpoch;
    std::uint32_t newEpoch = oldEpoch + 1;
    if (newEpoch == 0)
        newEpoch = 1;

    ownerComponent.SetOwner(nullptr);
    ownerComponent.OwnershipEpoch = newEpoch;
    ownerComponent.InvalidOwners.clear();

    NotifyOwnershipTransfer notify{};
    notify.ServerId = World::ToInteger(aEntity);
    notify.OwnerPlayerId = 0;
    notify.OwnershipEpoch = newEpoch;
    notify.CurrentActorData = BuildActorData(aEntity);
    notify.LeveledNpcPickId = view.get<CharacterComponent>(*it).LeveledNpcPickId.Id;

    if (!GameServer::Get()->SendToPlayersInRange(notify, aEntity, pOldOwner))
        spdlog::error("Failed to broadcast ownerless corpse state for actor {:X}", notify.ServerId);
    pOldOwner->Send(notify);

    spdlog::info(
        "[CorpseRetention] actor {:X} is ownerless after {} (epoch {} to {}, expiry tick {})",
        notify.ServerId, GetOwnershipTransferReasonName(aReason), oldEpoch, newEpoch,
        view.get<CorpseRetentionComponent>(*it).ExpiresAtTick);
    return true;
}

ActorData CharacterService::BuildActorData(const entt::entity acEntity) const noexcept
{
    ActorData actorData{};

    const auto* pActorValuesComponent = m_world.try_get<ActorValuesComponent>(acEntity);
    if (pActorValuesComponent)
    {
        actorData.InitialActorValues = pActorValuesComponent->CurrentActorValues;
    }

    const auto* pInventoryComponent = m_world.try_get<InventoryComponent>(acEntity);
    if (pInventoryComponent)
    {
        actorData.InitialInventory = pInventoryComponent->Content;
    }

    actorData.IsDead = false;
    const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(acEntity);
    if (pCharacterComponent)
    {
        actorData.IsDead = pCharacterComponent->IsDead();
        actorData.IsWeaponDrawn = pCharacterComponent->IsWeaponDrawn();
    }

    return actorData;
}

void CharacterService::ProcessFactionsChanges() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 2000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    const auto characterView = m_world.view<CellIdComponent, CharacterComponent, OwnerComponent>();

    TiltedPhoques::Map<Player*, NotifyFactionsChanges> messages;

    for (auto entity : characterView)
    {
        auto& characterComponent = characterView.get<CharacterComponent>(entity);
        auto& cellIdComponent = characterView.get<CellIdComponent>(entity);
        auto& ownerComponent = characterView.get<OwnerComponent>(entity);

        // If we have nothing new to send skip this
        if (!characterComponent.IsDirtyFactions())
            continue;

        for (auto pPlayer : m_world.GetPlayerManager())
        {
            if (pPlayer == ownerComponent.GetOwner())
                continue;

            if (!cellIdComponent.IsInRange(pPlayer->GetCellComponent(), characterComponent.IsDragon()))
                continue;

            auto& message = messages[pPlayer];
            auto& change = message.Changes[World::ToInteger(entity)];
            change.OwnershipEpoch = ownerComponent.OwnershipEpoch;
            change.FactionsContent = characterComponent.FactionsContent;
        }

        characterComponent.SetDirtyFactions(false);
    }

    for (auto [pPlayer, message] : messages)
    {
        if (!message.Changes.empty())
            pPlayer->Send(message);
    }
}

void CharacterService::ProcessMovementChanges() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 1000ms / 50;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    const auto characterView = m_world.view<CharacterComponent, CellIdComponent, MovementComponent, AnimationComponent, OwnerComponent>();

    TiltedPhoques::Map<Player*, ServerReferencesMoveRequest> messages;

    for (auto pPlayer : m_world.GetPlayerManager())
    {
        auto& message = messages[pPlayer];

        message.Tick = GameServer::Get()->GetTick();
    }

    for (auto entity : characterView)
    {
        auto& characterComponent = characterView.get<CharacterComponent>(entity);
        auto& movementComponent = characterView.get<MovementComponent>(entity);
        auto& cellIdComponent = characterView.get<CellIdComponent>(entity);
        auto& ownerComponent = characterView.get<OwnerComponent>(entity);
        auto& animationComponent = characterView.get<AnimationComponent>(entity);

        // If we have nothing new to send skip this
        if (movementComponent.Sent == true)
            continue;

        for (auto pPlayer : m_world.GetPlayerManager())
        {
            if (pPlayer == ownerComponent.GetOwner())
                continue;

            if (!cellIdComponent.IsInRange(pPlayer->GetCellComponent(), characterComponent.IsDragon()))
                continue;

            auto& message = messages[pPlayer];
            auto& update = message.Updates[World::ToInteger(entity)];
            auto& movement = update.UpdatedMovement;

            update.OwnershipEpoch = ownerComponent.OwnershipEpoch;

            movement.CellId = cellIdComponent.Cell;
            movement.WorldSpaceId = cellIdComponent.WorldSpaceId;
            movement.Position = movementComponent.Position;

            movement.Rotation.x = movementComponent.Rotation.x;
            movement.Rotation.y = movementComponent.Rotation.z;

            movement.Direction = movementComponent.Direction;
            movement.Variables = movementComponent.Variables;

            update.ActionEvents = animationComponent.Actions;

            if (!MovementAuthorityPolicy::HasValidPayload(update))
            {
                spdlog::warn("Skipped malformed server movement update for actor {:X}", World::ToInteger(entity));
                message.Updates.erase(World::ToInteger(entity));
            }
        }
    }

    m_world.view<AnimationComponent>().each([](AnimationComponent& animationComponent)
    {
        // Remove actions we've sent
        animationComponent.Actions.clear();
    });

    m_world.view<MovementComponent>().each([](MovementComponent& movementComponent) { movementComponent.Sent = true; });

    for (auto& [pPlayer, message] : messages)
    {
        if (!message.Updates.empty())
            pPlayer->Send(message);
    }
}
