#pragma once

#include <Events/EventDispatcher.h>
#include <Games/Events.h>

#include <Structs/GameId.h>

#include <cstdint>

struct TESObjectREFR;
struct TESObjectCELL;
struct ServerTimeSettings;
struct DisconnectedEvent;
struct World;
struct ActivateEvent;
struct TransportService;
struct NotifyActivate;
struct LockChangeEvent;
struct NotifyLockChange;
struct CellChangeEvent;
struct UpdateEvent;
struct ScriptAnimationEvent;
struct AssignObjectsResponse;
struct NotifyScriptAnimation;
struct NotifyObjectHarvested;
struct NotifyWorldItemTaken;
struct CharacterWorldSyncStartedEvent;

/**
 * @brief Handles objects in the environment.
 */
class ObjectService final : public BSTEventSink<TESActivateEvent>
{
public:
    ObjectService(World&, entt::dispatcher&, TransportService&);

private:
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnCellChange(const CellChangeEvent&) noexcept;
    void OnWorldSyncStarted(const CharacterWorldSyncStartedEvent&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void SendAssignObjectsRequest() noexcept;
    void SendObjectStateReport() noexcept;
    void OnAssignObjectsResponse(const AssignObjectsResponse&) noexcept;
    void OnActivate(const ActivateEvent&) noexcept;
    void OnActivateNotify(const NotifyActivate&) noexcept;
    void OnLockChange(const LockChangeEvent&) noexcept;
    void OnLockChangeNotify(const NotifyLockChange&) noexcept;
    void OnScriptAnimationEvent(const ScriptAnimationEvent&) noexcept;
    void OnNotifyScriptAnimation(const NotifyScriptAnimation&) noexcept;
    void OnObjectHarvestedNotify(const NotifyObjectHarvested&) noexcept;
    void OnWorldItemTakenNotify(const NotifyWorldItemTaken&) noexcept;

    BSTEventResult OnEvent(const TESActivateEvent*, const EventDispatcher<TESActivateEvent>*) override;

    entt::entity CreateObjectEntity(const uint32_t acFormId, const uint32_t acServerId) noexcept;

    // A reference in the loaded cells that is registered with the server.
    struct SyncedObject
    {
        TESObjectREFR* pObject{};
        GameId CellId{};
        GameId Id{};
        bool IsHarvestType{};
        bool IsOpenLoot{};
    };
    bool CollectSyncedObjects(Vector<SyncedObject>& aObjects, GameId& aWorldSpaceId, TESObjectCELL*& apCell, size_t& aCellCount) noexcept;

    World& m_world;
    TransportService& m_transport;
    // Set on a cell change and at world entry (first join and every reconnect), sent on the next
    // in-world update: PlayerService reports the cell in the same dispatch but is connected after us,
    // and the server range-checks against it. The response is the server's full state of the cells.
    bool m_assignObjectsPending{false};
    // Desync detector report period, in seconds.
    static constexpr double kStateReportInterval = 5.0;
    double m_stateReportTimer{};

    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_cellChangeConnection;
    entt::scoped_connection m_worldSyncStartedConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_onActivateConnection;
    entt::scoped_connection m_activateConnection;
    entt::scoped_connection m_lockChangeConnection;
    entt::scoped_connection m_lockChangeNotifyConnection;
    entt::scoped_connection m_assignObjectConnection;
    entt::scoped_connection m_scriptAnimationConnection;
    entt::scoped_connection m_scriptAnimationNotifyConnection;
    entt::scoped_connection m_objectHarvestedConnection;
    entt::scoped_connection m_worldItemTakenConnection;
};

void TrackLocalWorldItemTaken(std::uint32_t aFormId) noexcept;
