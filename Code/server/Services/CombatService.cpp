#include <Services/CombatService.h>
#include <Services/CombatAttackerAuthorizationPolicy.h>
#include <Services/CombatTargetAuthorizationPolicy.h>
#include <Services/ProjectileLaunchAuthorityPolicy.h>
#include <Components.h>
#include <GameServer.h>
#include <Game/Player.h>
#include <World.h>

#include <Messages/CombatHitObservationRequest.h>
#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/NotifyProjectileLaunch.h>

#include <limits>

CombatService::CombatService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_projectileLaunchConnection = aDispatcher.sink<PacketEvent<ProjectileLaunchRequest>>().connect<&CombatService::OnProjectileLaunchRequest>(this);
    m_hitObservationConnection = aDispatcher.sink<PacketEvent<CombatHitObservationRequest>>().connect<&CombatService::OnHitObservationRequest>(this);
}

void CombatService::OnHitObservationRequest(const PacketEvent<CombatHitObservationRequest>& acMessage) noexcept
{
    const auto& packet = acMessage.Packet;
    auto* const pPlayer = acMessage.GetSender();
    if (!pPlayer || packet.AttackerServerId == packet.TargetServerId || !m_pendingObservations.CanAppend() ||
        m_observationTick == std::numeric_limits<ValidatedHitObservation::ObservationTick>::max())
        return;

    const auto attackerEntity = static_cast<entt::entity>(packet.AttackerServerId);
    const auto attackerView = m_world.view<CharacterComponent, OwnerComponent, PersistentCharacterComponent>();
    const auto attackerIt = attackerView.find(attackerEntity);
    const bool attackerExists = attackerIt != attackerView.end();
    const auto* const pSession = m_world.GetSessionService().Get(pPlayer->GetConnectionId());

    CombatAttackerAuthorizationInput attackerInput{};
    attackerInput.SessionIsInWorld = pSession && pSession->State == SessionState::kInWorld;
    if (pSession)
        attackerInput.SessionCharacterId = pSession->SelectedCharacterId;
    attackerInput.AttackerServerId = packet.AttackerServerId;
    attackerInput.AttackerEntityExists = attackerExists;
    if (attackerExists)
    {
        const auto& character = attackerView.get<CharacterComponent>(*attackerIt);
        const auto& owner = attackerView.get<OwnerComponent>(*attackerIt);
        const auto& persistentCharacter = attackerView.get<PersistentCharacterComponent>(*attackerIt);
        attackerInput.AttackerIsPlayerCharacter = character.IsPlayer();
        attackerInput.OwnerExists = true;
        attackerInput.SenderIsCurrentOwner = owner.IsCurrentOwner(pPlayer, packet.AttackerOwnershipEpoch);
        attackerInput.RequestedOwnershipEpoch = packet.AttackerOwnershipEpoch;
        attackerInput.CurrentOwnershipEpoch = owner.OwnershipEpoch;
        attackerInput.ServerResolvedPersistentCharacterId = persistentCharacter.CharacterId;
    }

    if (!CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(attackerInput).has_value())
        return;

    const auto targetEntity = static_cast<entt::entity>(packet.TargetServerId);
    const bool targetExists = m_world.valid(targetEntity);
    CombatTargetAuthorizationInput targetInput{};
    targetInput.TargetServerId = packet.TargetServerId;
    targetInput.TargetEntityExists = targetExists;
    targetInput.ObservedTargetLifecycleGeneration = packet.TargetLifecycleGeneration;
    targetInput.pCurrentTargetLifecycle = m_world.try_get<ActorLifecycleComponent>(targetEntity);
    targetInput.pTargetPopulationIdentity = m_world.try_get<ActorPopulationIdentityComponent>(targetEntity);
    targetInput.pAttackerCell = attackerExists ? m_world.try_get<CellIdComponent>(attackerEntity) : nullptr;
    targetInput.pTargetCell = targetExists ? m_world.try_get<CellIdComponent>(targetEntity) : nullptr;
    if (!CombatTargetAuthorizationPolicy::IsAuthorized(targetInput))
        return;

    const auto observedTick = m_observationTick + 1;
    const ValidatedHitObservation observation{
        packet.AttackerServerId,
        packet.AttackerOwnershipEpoch,
        packet.TargetServerId,
        packet.TargetLifecycleGeneration,
        packet.ObservationId,
        observedTick};

    if (!observation.IsWellFormed() || !m_observationReplayCache.TryRemember(observation))
        return;

    // Capacity was checked above on this synchronous dispatcher path, so this
    // append cannot evict an earlier pending observation.
    if (m_pendingObservations.TryAppend(observation))
        m_observationTick = observedTick;
}

void CombatService::OnProjectileLaunchRequest(const PacketEvent<ProjectileLaunchRequest>& acMessage) const noexcept
{
    auto& packet = acMessage.Packet;

    auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto shooterIt = characterView.find(static_cast<entt::entity>(packet.ShooterID));
    const bool shooterExists = shooterIt != characterView.end();
    const bool isCurrentOwner = shooterExists && characterView.get<OwnerComponent>(*shooterIt).IsCurrentOwner(acMessage.pPlayer, packet.OwnershipEpoch);
    if (!ProjectileLaunchAuthorityPolicy::IsAuthorized(shooterExists, shooterExists, isCurrentOwner, packet.OwnershipEpoch))
        return;

    if (!ProjectileLaunchAuthorityPolicy::HasFiniteParameters(
            packet.OriginX, packet.OriginY, packet.OriginZ, packet.ZAngle, packet.XAngle, packet.YAngle, packet.Power, packet.Scale))
        return;

    if (packet.CastingSource < 0 || packet.CastingSource >= 4)
        return;

    NotifyProjectileLaunch notify{};

    notify.ShooterID = packet.ShooterID;
    notify.OwnershipEpoch = packet.OwnershipEpoch;

    notify.OriginX = packet.OriginX;
    notify.OriginY = packet.OriginY;
    notify.OriginZ = packet.OriginZ;

    notify.ProjectileBaseID = packet.ProjectileBaseID;
    notify.WeaponID = packet.WeaponID;
    notify.AmmoID = packet.AmmoID;

    notify.ZAngle = packet.ZAngle;
    notify.XAngle = packet.XAngle;
    notify.YAngle = packet.YAngle;

    notify.ParentCellID = packet.ParentCellID;

    notify.SpellID = packet.SpellID;
    notify.CastingSource = packet.CastingSource;

    notify.Area = packet.Area;
    notify.Power = packet.Power;
    notify.Scale = packet.Scale;

    notify.AlwaysHit = packet.AlwaysHit;
    notify.NoDamageOutsideCombat = packet.NoDamageOutsideCombat;
    notify.AutoAim = packet.AutoAim;
    notify.DeferInitialization = packet.DeferInitialization;
    notify.ForceConeOfFire = packet.ForceConeOfFire;

    notify.UnkBool1 = packet.UnkBool1;
    notify.UnkBool2 = packet.UnkBool2;

    const auto cShooterEntity = static_cast<entt::entity>(packet.ShooterID);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cShooterEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
