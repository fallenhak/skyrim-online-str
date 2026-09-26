#pragma once

#include <Events/PacketEvent.h>
#include <Persistence/WorldObjectRepository.h>
#include <Services/DesyncPolicy.h>
#include <Structs/ObjectData.h>

#include <unordered_map>
#include <vector>

struct World;
struct Player;
struct ObjectComponent;
struct PlayerLeaveCellEvent;
struct PlayerLeaveEvent;
struct ActivateRequest;
struct TakeWorldItemRequest;
struct LockChangeRequest;
struct AssignObjectsRequest;
struct ScriptAnimationRequest;
struct ObjectStateReport;
struct UpdateEvent;

/**
 * @brief Manages (interactive) objects and relays interactions with said objects.
 */
class ObjectService
{
public:
    ObjectService(World& aWorld, entt::dispatcher& aDispatcher, Persistence::WorldObjectRepository& aRepository);

    // Called after an accepted container transfer: queues the container's server-owned
    // contents for write-back so they survive a server restart.
    void PersistContainerContents(entt::entity aEntity) noexcept;

private:
    void OnPlayerLeaveCellEvent(const PlayerLeaveCellEvent& acEvent) noexcept;
    void OnAssignObjectsRequest(const PacketEvent<AssignObjectsRequest>&) noexcept;
    void OnActivate(const PacketEvent<ActivateRequest>&) noexcept;
    void OnTakeWorldItem(const PacketEvent<TakeWorldItemRequest>&) noexcept;
    void OnLockChange(const PacketEvent<LockChangeRequest>&) const noexcept;
    void OnScriptAnimationRequest(const PacketEvent<ScriptAnimationRequest>&) noexcept;
    void OnObjectStateReport(const PacketEvent<ObjectStateReport>&) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent&) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void RespawnWorldObjects() noexcept;
    void PruneUnobservedObjects() noexcept;
    void RestorePersistedStates();
    void ApplyPersistedState(ObjectComponent& aObject, const Persistence::WorldObjectState& acState) noexcept;
    void PersistState(entt::entity aEntity) noexcept;
    // The server's full state of one registered object, as sent in a cell snapshot or a correction.
    ObjectData BuildObjectData(entt::entity aEntity) const noexcept;
    // Compares a reported corpse with the server's contents and sends them back when they differ.
    bool CheckCorpseDigest(Player& aPlayer, const ObjectStateDigest& acDigest, const std::unordered_map<GameId, entt::entity>& acCharactersById) noexcept;
    [[nodiscard]] const Persistence::WorldObjectState* FindPersistedState(const GameId& acId, const GameId& acCellId) const noexcept;
    [[nodiscard]] const Persistence::ContainerContentsState* FindPersistedContainer(const GameId& acId, const GameId& acCellId) const noexcept;

    World& m_world;
    Persistence::WorldObjectRepository& m_repository;
    std::vector<Persistence::WorldObjectState> m_persistedStates;
    std::vector<Persistence::ContainerContentsState> m_persistedContainers;
    std::uint64_t m_tick{};
    double m_tickAccumulator{};
    DesyncPolicy::Tracker m_desyncTracker;

    entt::scoped_connection m_leaveCellConnection;
    entt::scoped_connection m_assignObjectConnection;
    entt::scoped_connection m_activateConnection;
    entt::scoped_connection m_takeWorldItemConnection;
    entt::scoped_connection m_lockChangeConnection;
    entt::scoped_connection m_scriptAnimationConnection;
    entt::scoped_connection m_objectStateReportConnection;
    entt::scoped_connection m_playerLeaveConnection;
    entt::scoped_connection m_updateConnection;
};
