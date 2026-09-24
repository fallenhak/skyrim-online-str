#pragma once

#include <Events/PacketEvent.h>
#include <Services/CombatContributionLedger.h>
#include <Services/CombatObservationReplayCache.h>
#include <Services/PendingCombatObservationStore.h>

#include <cstddef>
#include <cstdint>

struct World;
struct ProjectileLaunchRequest;
struct CombatHitObservationRequest;
struct AcceptedCanonicalHealthDecreaseEvent;
struct AcceptedCanonicalCreatureDeathEvent;
struct CorrelatedCombatObservationEvent;
struct CharacterRemoveEvent;
struct ActorRespawnedEvent;

struct CombatService
{
    CombatService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~CombatService() noexcept = default;

    TP_NOCOPYMOVE(CombatService);

protected:
    void OnProjectileLaunchRequest(const PacketEvent<ProjectileLaunchRequest>& acMessage) const noexcept;
    void OnHitObservationRequest(const PacketEvent<CombatHitObservationRequest>& acMessage) noexcept;
    void OnCanonicalHealthDecrease(const AcceptedCanonicalHealthDecreaseEvent& acEvent) noexcept;
    void OnCorrelatedCombatObservation(const CorrelatedCombatObservationEvent& acEvent) noexcept;
    void OnAcceptedCreatureDeath(const AcceptedCanonicalCreatureDeathEvent& acEvent) noexcept;
    void OnCharacterRemove(const CharacterRemoveEvent& acEvent) noexcept;
    void OnActorRespawned(const ActorRespawnedEvent& acEvent) noexcept;
    void ClearActorCombatState(std::uint32_t aServerId) noexcept;

private:
    static constexpr std::size_t kPendingObservationCapacity = 1024;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    CombatObservationReplayCache<> m_observationReplayCache;
    PendingCombatObservationStore<kPendingObservationCapacity> m_pendingObservations;
    CombatContributionLedger m_contributionLedger;
    ValidatedHitObservation::ObservationTick m_observationTick{};

    entt::scoped_connection m_projectileLaunchConnection;
    entt::scoped_connection m_hitObservationConnection;
    entt::scoped_connection m_healthDecreaseConnection;
    entt::scoped_connection m_correlatedObservationConnection;
    entt::scoped_connection m_creatureDeathConnection;
    entt::scoped_connection m_characterRemoveConnection;
    entt::scoped_connection m_actorRespawnedConnection;
};
