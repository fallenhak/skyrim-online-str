#pragma once

#include <Events/PacketEvent.h>
#include <Services/CombatObservationReplayCache.h>
#include <Services/PendingCombatObservationStore.h>

#include <cstddef>

struct World;
struct ProjectileLaunchRequest;
struct CombatHitObservationRequest;

struct CombatService
{
    CombatService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~CombatService() noexcept = default;

    TP_NOCOPYMOVE(CombatService);

protected:
    void OnProjectileLaunchRequest(const PacketEvent<ProjectileLaunchRequest>& acMessage) const noexcept;
    void OnHitObservationRequest(const PacketEvent<CombatHitObservationRequest>& acMessage) noexcept;

private:
    static constexpr std::size_t kPendingObservationCapacity = 1024;

    World& m_world;
    CombatObservationReplayCache<> m_observationReplayCache;
    PendingCombatObservationStore<kPendingObservationCapacity> m_pendingObservations;
    ValidatedHitObservation::ObservationTick m_observationTick{};

    entt::scoped_connection m_projectileLaunchConnection;
    entt::scoped_connection m_hitObservationConnection;
};
