#pragma once

#include <include/internal/cef_ptr.h>
#include <cstdint>

namespace TiltedPhoques
{
struct OverlayApp;
}

struct RenderSystemD3D11;
struct D3D11RenderProvider;
struct FormIdComponent;
struct World;
struct Actor;
struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct TransportService;
struct NotifyChatMessageBroadcast;
struct NotifyPlayerList;
struct NotifyPlayerJoined;
struct NotifyPlayerLeft;
struct NotifyPlayerDialogue;
struct ConnectionErrorEvent;
struct NotifyPlayerLevel;
struct NotifyPlayerCellChanged;
struct NotifyTeleport;
struct NotifyPlayerHealthUpdate;
struct CharacterListReceivedEvent;
struct CharacterSlotsReceivedEvent;
struct CharacterCreateResultEvent;
struct CharacterSelectionResultEvent;
struct CharacterSessionStateChangedEvent;
struct LoadingStageEvent;
enum ChatMessageTypes;
struct PartyJoinedEvent;
struct PartyLeftEvent;

using TiltedPhoques::OverlayApp;

/**
 * @brief Controls the UI from the client.
 */
struct OverlayService
{
    OverlayService(World& aWorld, TransportService& transport, entt::dispatcher& aDispatcher);
    ~OverlayService() noexcept;

    TP_NOCOPYMOVE(OverlayService);

    void Create(RenderSystemD3D11* apRenderSystem) noexcept;

    void Render() noexcept;
    void Reset() const noexcept;
    void Reload() noexcept;

    void Initialize() noexcept;

    void SetActive(bool aActive) noexcept;
    [[nodiscard]] bool GetActive() const noexcept;

    void SetInGame(bool aInGame) noexcept;
    [[nodiscard]] bool GetInGame() const noexcept;

    // Title-menu entry screen: the overlay must take input before a player exists.
    void SetEntryActive(bool aActive) noexcept;
    // Hands mouse/keyboard back to Skyrim once the entry flow reaches a native menu or the world.
    void ReleaseEntryInput() noexcept;

    void SetVersion(const std::string& acVersion);

    OverlayApp* GetOverlayApp() const noexcept { return m_pOverlay.get(); }

    void SendSystemMessage(const std::string& acMessage);

    void EmitAuthState(const std::string& acState, const std::string& acDisplayName = "",
        const std::string& acAvatarUrl = "", const std::string& acErrorKey = "");

    void SetPlayerHealthPercentage(uint32_t aFormId) const noexcept;

protected:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnConnectedEvent(const ConnectedEvent&) noexcept;
    void OnDisconnectedEvent(const DisconnectedEvent&) noexcept;
    void OnWaitingFor3DRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept;
    void OnPlayerComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept;
    void OnConnectionError(const ConnectionErrorEvent& acConnectedEvent) noexcept;
    void OnChatMessageReceived(const NotifyChatMessageBroadcast&) noexcept;
    void OnPlayerDialogue(const NotifyPlayerDialogue&) noexcept;
    void OnPlayerJoined(const NotifyPlayerJoined&) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft&) noexcept;
    void OnPlayerLevel(const NotifyPlayerLevel&) noexcept;
    void OnPlayerCellChanged(const NotifyPlayerCellChanged& acMessage) const noexcept;
    void OnNotifyTeleport(const NotifyTeleport& acMessage) noexcept;
    void OnNotifyPlayerHealthUpdate(const NotifyPlayerHealthUpdate& acMessage) noexcept;
    void OnCharacterListReceived(const CharacterListReceivedEvent& acEvent) noexcept;
    void OnCharacterSlotsReceived(const CharacterSlotsReceivedEvent& acEvent) noexcept;
    void OnCharacterCreateResult(const CharacterCreateResultEvent& acEvent) noexcept;
    void OnCharacterSelectionResult(const CharacterSelectionResultEvent& acEvent) noexcept;
    void OnCharacterSessionStateChanged(const CharacterSessionStateChangedEvent& acEvent) noexcept;
    void OnLoadingStage(const LoadingStageEvent& acEvent) noexcept;
    void OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept;
    void OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept;

private:
    void RunDebugDataUpdates() noexcept;
    void RunPlayerHealthUpdates() noexcept;

    CefRefPtr<OverlayApp> m_pOverlay{nullptr};
    TiltedPhoques::UniquePtr<D3D11RenderProvider> m_pProvider;

    World& m_world;
    TransportService& m_transport;

    bool m_active = false;
    bool m_inGame = false;
    bool m_entryActive = false;
    bool m_entryInputApplied = false;
    std::uint32_t m_characterConnectionGeneration = 0;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_connectionErrorConnection;
    entt::scoped_connection m_cellChangeEventConnection;
    entt::scoped_connection m_chatMessageConnection;
    entt::scoped_connection m_playerDialogueConnection;
    entt::scoped_connection m_playerJoinedConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_playerAddedConnection;
    entt::scoped_connection m_playerRemovedConnection;
    entt::scoped_connection m_playerLevelConnection;
    entt::scoped_connection m_cellChangedConnection;
    entt::scoped_connection m_teleportConnection;
    entt::scoped_connection m_playerHealthConnection;
    entt::scoped_connection m_characterListConnection;
    entt::scoped_connection m_characterSlotsConnection;
    entt::scoped_connection m_characterCreateResultConnection;
    entt::scoped_connection m_characterSelectionResultConnection;
    entt::scoped_connection m_characterSessionStateConnection;
    entt::scoped_connection m_loadingStageConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
};
