#include <Services/CombatService.h>
#include <Services/CombatAttackerAuthorizationPolicy.h>
#include <Services/CombatTargetAuthorizationPolicy.h>
#include <Services/ProjectileLaunchAuthorityPolicy.h>
#include <Events/AcceptedCanonicalHealthDecreaseEvent.h>
#include <Events/AcceptedCanonicalCreatureDeathEvent.h>
#include <Events/CorrelatedCombatObservationEvent.h>
#include <Events/CreatureDeathContributionEvent.h>
#include <Components.h>
#include <GameServer.h>
#include <Game/Player.h>
#include <World.h>

#include <Messages/CombatHitObservationRequest.h>
#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/NotifyProjectileLaunch.h>

#include <limits>
#include <optional>
#include <utility>

namespace
{
std::optional<Persistence::CharacterId> ResolveAuthorizedAttackerCharacterId(
    World& aWorld,
    const ValidatedHitObservation::ServerId aAttackerServerId,
    const ValidatedHitObservation::OwnershipEpoch aOwnershipEpoch,
    Player* apSender,
    ValidatedHitObservation::LifecycleGeneration& aResolvedAttackerLifecycleGeneration) noexcept
{
    if (!apSender || aAttackerServerId == 0)
        return std::nullopt;

    const auto attackerEntity = static_cast<entt::entity>(aAttackerServerId);
    const auto attackerView = aWorld.view<
        CharacterComponent, OwnerComponent, PersistentCharacterComponent, ActorLifecycleComponent>();
    const auto attackerIt = attackerView.find(attackerEntity);
    if (attackerIt == attackerView.end())
        return std::nullopt;

    const auto& character = attackerView.get<CharacterComponent>(*attackerIt);
    const auto& owner = attackerView.get<OwnerComponent>(*attackerIt);
    const auto& persistentCharacter = attackerView.get<PersistentCharacterComponent>(*attackerIt);
    const auto& lifecycle = attackerView.get<ActorLifecycleComponent>(*attackerIt);
    if (!lifecycle.IsValid())
        return std::nullopt;

    const auto* const pSession = aWorld.GetSessionService().Get(apSender->GetConnectionId());

    CombatAttackerAuthorizationInput attackerInput{};
    attackerInput.SessionIsInWorld = pSession && pSession->State == SessionState::kInWorld;
    if (pSession)
        attackerInput.SessionCharacterId = pSession->SelectedCharacterId;
    attackerInput.AttackerServerId = aAttackerServerId;
    attackerInput.AttackerEntityExists = true;
    attackerInput.AttackerIsPlayerCharacter = character.IsPlayer();
    attackerInput.OwnerExists = true;
    attackerInput.SenderIsCurrentOwner = owner.IsCurrentOwner(apSender, aOwnershipEpoch);
    attackerInput.RequestedOwnershipEpoch = aOwnershipEpoch;
    attackerInput.CurrentOwnershipEpoch = owner.OwnershipEpoch;
    attackerInput.ServerResolvedPersistentCharacterId = persistentCharacter.CharacterId;

    const auto characterId = CombatAttackerAuthorizationPolicy::ResolveAuthorizedCharacterId(attackerInput);
    if (characterId)
        aResolvedAttackerLifecycleGeneration = lifecycle.GetGeneration();
    return characterId;
}

std::optional<Persistence::CharacterId> ResolveCurrentObservationAttackerCharacterId(
    World& aWorld,
    const ValidatedHitObservation& acObservation) noexcept
{
    if (!acObservation.IsWellFormed())
        return std::nullopt;

    const auto attackerEntity = static_cast<entt::entity>(acObservation.AttackerServerId);
    const auto attackerView = aWorld.view<OwnerComponent>();
    const auto attackerIt = attackerView.find(attackerEntity);
    if (attackerIt == attackerView.end())
        return std::nullopt;

    ValidatedHitObservation::LifecycleGeneration attackerLifecycleGeneration{};
    const auto attackerCharacterId = ResolveAuthorizedAttackerCharacterId(
        aWorld,
        acObservation.AttackerServerId,
        acObservation.AttackerOwnershipEpoch,
        attackerView.get<OwnerComponent>(*attackerIt).GetOwner(),
        attackerLifecycleGeneration);
    if (!attackerCharacterId || !acObservation.IsFromAttackerIncarnation(attackerLifecycleGeneration))
        return std::nullopt;

    return attackerCharacterId;
}
} // namespace

CombatService::CombatService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_projectileLaunchConnection = aDispatcher.sink<PacketEvent<ProjectileLaunchRequest>>().connect<&CombatService::OnProjectileLaunchRequest>(this);
    m_hitObservationConnection = aDispatcher.sink<PacketEvent<CombatHitObservationRequest>>().connect<&CombatService::OnHitObservationRequest>(this);
    m_healthDecreaseConnection = aDispatcher.sink<AcceptedCanonicalHealthDecreaseEvent>().connect<&CombatService::OnCanonicalHealthDecrease>(this);
    m_correlatedObservationConnection = aDispatcher.sink<CorrelatedCombatObservationEvent>().connect<&CombatService::OnCorrelatedCombatObservation>(this);
    m_creatureDeathConnection = aDispatcher.sink<AcceptedCanonicalCreatureDeathEvent>().connect<&CombatService::OnAcceptedCreatureDeath>(this);
}

void CombatService::OnHitObservationRequest(const PacketEvent<CombatHitObservationRequest>& acMessage) noexcept
{
    const auto& packet = acMessage.Packet;
    auto* const pPlayer = acMessage.GetSender();
    if (!pPlayer || packet.AttackerServerId == packet.TargetServerId || !m_pendingObservations.CanAppend() ||
        m_observationTick == std::numeric_limits<ValidatedHitObservation::ObservationTick>::max())
        return;

    ValidatedHitObservation::LifecycleGeneration attackerLifecycleGeneration{};
    const auto attackerCharacterId = ResolveAuthorizedAttackerCharacterId(
        m_world,
        packet.AttackerServerId,
        packet.AttackerOwnershipEpoch,
        pPlayer,
        attackerLifecycleGeneration);
    if (!attackerCharacterId)
        return;

    const auto attackerEntity = static_cast<entt::entity>(packet.AttackerServerId);

    const auto targetEntity = static_cast<entt::entity>(packet.TargetServerId);
    const bool targetExists = m_world.valid(targetEntity);
    CombatTargetAuthorizationInput targetInput{};
    targetInput.TargetServerId = packet.TargetServerId;
    targetInput.TargetEntityExists = targetExists;
    targetInput.ObservedTargetLifecycleGeneration = packet.TargetLifecycleGeneration;
    targetInput.pCurrentTargetLifecycle = m_world.try_get<ActorLifecycleComponent>(targetEntity);
    targetInput.pTargetPopulationIdentity = m_world.try_get<ActorPopulationIdentityComponent>(targetEntity);
    targetInput.pAttackerCell = m_world.try_get<CellIdComponent>(attackerEntity);
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
        observedTick,
        attackerLifecycleGeneration};

    if (!observation.IsWellFormed() || !m_observationReplayCache.TryRemember(observation))
        return;

    // Capacity was checked above on this synchronous dispatcher path, so this
    // append cannot evict an earlier pending observation.
    if (m_pendingObservations.TryAppend(observation))
        m_observationTick = observedTick;
}

void CombatService::OnCanonicalHealthDecrease(const AcceptedCanonicalHealthDecreaseEvent& acEvent) noexcept
{
    const auto targetEntity = static_cast<entt::entity>(acEvent.TargetServerId);
    if (!m_world.valid(targetEntity))
        return;

    const auto* const pLifecycle = m_world.try_get<ActorLifecycleComponent>(targetEntity);
    if (!pLifecycle || !pLifecycle->IsValid() || pLifecycle->GetGeneration() != acEvent.TargetLifecycleGeneration)
        return;

    const auto observation = m_pendingObservations.TakeForAcceptedHealthDecrease(
        acEvent.TargetServerId,
        acEvent.TargetLifecycleGeneration,
        [this](const ValidatedHitObservation& acObservation) noexcept {
            return ResolveCurrentObservationAttackerCharacterId(m_world, acObservation).has_value();
        });
    if (!observation)
        return;

    // The correlated event contains only the validated observation identity.
    // In particular, no submitted health delta or damage magnitude crosses
    // this boundary.
    m_dispatcher.trigger(CorrelatedCombatObservationEvent{*observation});
}

void CombatService::OnCorrelatedCombatObservation(const CorrelatedCombatObservationEvent& acEvent) noexcept
{
    const auto observation = acEvent.ToObservation();
    if (!observation.IsWellFormed() || observation.ObservedTick == 0 ||
        observation.AttackerServerId == observation.TargetServerId)
        return;

    const auto targetEntity = static_cast<entt::entity>(observation.TargetServerId);
    if (!m_world.valid(targetEntity))
        return;

    const auto* const pTargetLifecycle = m_world.try_get<ActorLifecycleComponent>(targetEntity);
    if (!pTargetLifecycle || !pTargetLifecycle->IsValid() ||
        pTargetLifecycle->GetGeneration() != observation.TargetLifecycleGeneration)
        return;

    const auto attackerCharacterId = ResolveCurrentObservationAttackerCharacterId(m_world, observation);
    if (!attackerCharacterId)
        return;

    const CombatContributionLedger::Target target{
        observation.TargetServerId,
        observation.TargetLifecycleGeneration};
    (void)m_contributionLedger.RecordValidatedContribution(target, *attackerCharacterId, observation.ObservedTick);
}

void CombatService::OnAcceptedCreatureDeath(const AcceptedCanonicalCreatureDeathEvent& acEvent) noexcept
{
    if (acEvent.TargetServerId == 0 || acEvent.TargetLifecycleGeneration == 0)
        return;

    const auto targetEntity = static_cast<entt::entity>(acEvent.TargetServerId);
    if (!m_world.valid(targetEntity))
        return;

    const auto* const pCharacter = m_world.try_get<CharacterComponent>(targetEntity);
    const auto* const pPopulationIdentity = m_world.try_get<ActorPopulationIdentityComponent>(targetEntity);
    const auto* const pLifecycle = m_world.try_get<ActorLifecycleComponent>(targetEntity);
    if (!pCharacter || !pCharacter->IsDead() || pCharacter->IsPlayer() || pCharacter->IsMount() || pCharacter->IsPlayerSummon() ||
        !pPopulationIdentity || !pPopulationIdentity->IsTrustedCreature() || !pLifecycle || !pLifecycle->IsValid() ||
        pLifecycle->GetGeneration() != acEvent.TargetLifecycleGeneration ||
        pLifecycle->AcceptedCanonicalCreatureDeathGeneration != acEvent.TargetLifecycleGeneration)
        return;

    const CombatContributionLedger::Target target{
        acEvent.TargetServerId,
        acEvent.TargetLifecycleGeneration};
    auto contributorCharacterIds = m_contributionLedger.ConsumeCharacterIdsForDeath(target, m_observationTick);
    m_dispatcher.trigger(CreatureDeathContributionEvent{std::move(contributorCharacterIds)});
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
