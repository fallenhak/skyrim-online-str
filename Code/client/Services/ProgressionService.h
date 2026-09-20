#pragma once

#include <Structs/ProgressionAwardPolicy.h>

#include <entt/entt.hpp>

struct DisconnectedEvent;
struct NotifyProgressionAward;
struct World;

/**
 * @brief Validates server-issued progression awards before applying them to Skyrim.
 */
struct ProgressionService final
{
    ProgressionService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~ProgressionService() noexcept = default;

    TP_NOCOPYMOVE(ProgressionService);

private:
    void OnNotifyProgressionAward(const NotifyProgressionAward& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    ProgressionAwardDeduplication<> m_appliedAwards;
    entt::scoped_connection m_awardConnection;
    entt::scoped_connection m_disconnectedConnection;
};
