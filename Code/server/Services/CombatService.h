#pragma once

#include <Events/PacketEvent.h>
#include <Services/CombatObservationReplayCache.h>
#include <Services/PendingCombatObservationStore.h>

#include <cstddef>

struct World;
struct ProjectileLaunchRequest;
struct CombatHitObservationRequest;
struct AcceptedCanonicalHealthDecreaseEvent;

struct CombatService
{
    CombatService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~CombatService() noexcept = default;

    TP_NOCOPYMOVE(CombatService);

protected:
    void OnProjectileLaunchRequest(const PacketEvent<ProjectileLaunchRequest>& acMessage) const noexcept;
    void OnHitObservationRequest(const PacketEvent<CombatHitObservationRequest>& acMessage) noexcept;
    void OnCanonicalHealthDecrease(const AcceptedCanonicalHealthDecreaseEvent& acEvent) noexcept;

private:
    static constexpr std::size_t kPendingObservationCapacity = 1024;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    CombatObservationReplayCache<> m_observationReplayCache;
    PendingCombatObservationStore<kPendingObservationCapacity> m_pendingObservations;
    ValidatedHitObservation::ObservationTick m_observationTick{};

    entt::scoped_connection m_projectileLaunchConnection;
    entt::scoped_connection m_hitObservationConnection;
    entt::scoped_connection m_healthDecreaseConnection;
};
