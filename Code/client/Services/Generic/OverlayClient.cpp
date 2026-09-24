#include <TiltedOnlinePCH.h>

#include <OverlayRenderHandler.hpp>
#include <DInputHook.hpp>

#include <Services/OverlayClient.h>
#include <Services/CharacterSessionService.h>
#include <Services/TransportService.h>

#include <Messages/SendChatMessageRequest.h>
#include <Messages/TeleportRequest.h>

#include <Events/SetTimeCommandEvent.h>
#include <Events/LoadingStageEvent.h>

#include <World.h>

#include <charconv>
#include <cstdint>
#include <string>

namespace
{
bool TryParseCanonicalCharacterId(const std::string& aValue, std::uint64_t& aCharacterId) noexcept
{
    if (aValue.empty() || (aValue.size() > 1 && aValue.front() == '0'))
        return false;

    for (const char character : aValue)
    {
        if (character < '0' || character > '9')
            return false;
    }

    const auto result = std::from_chars(aValue.data(), aValue.data() + aValue.size(), aCharacterId);
    return result.ec == std::errc{} && result.ptr == aValue.data() + aValue.size();
}
} // namespace

OverlayClient::OverlayClient(TransportService& aTransport, TiltedPhoques::OverlayRenderHandler* apHandler)
    : TiltedPhoques::OverlayClient(apHandler)
    , m_transport(aTransport)
{
}

OverlayClient::~OverlayClient() noexcept
{
}

bool OverlayClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
{
    if (message->GetName() == "ui-event")
    {
        auto pArguments = message->GetArgumentList();

        auto eventName = pArguments->GetString(0).ToString();
        spdlog::info("[UI] event '{}'", eventName);
        auto eventArgs = pArguments->GetList(1);

#ifndef PUBLIC_BUILD
        LOG(INFO) << "event=ui_event name=" << eventName;
#endif

        if (eventName == "connect")
            ProcessConnectMessage(eventArgs);
        else if (eventName == "disconnect")
            ProcessDisconnectMessage();
        else if (eventName == "requestCharacterList")
        {
            World::Get().GetDispatcher().trigger(LoadingStageEvent{LoadingStage::kFetchingCharacters, 0.35f});
            World::Get().GetRunner().Queue([]() {
                if (!World::Get().GetCharacterSessionService().RequestCharacterList())
                    spdlog::debug("Character list request was rejected by the current client session state.");
            });
        }
        else if (eventName == "selectCharacter")
            ProcessSelectCharacterMessage(eventArgs);
        else if (eventName == "createCharacter")
            ProcessCreateCharacterMessage(eventArgs);
        else if (eventName == "retryConnect")
            ProcessRetryConnectMessage();
        else if (eventName == "quitGame")
            ProcessQuitGameMessage();
        else if (eventName == "revealPlayers")
            ProcessRevealPlayersMessage();
        else if (eventName == "sendMessage")
            ProcessChatMessage(eventArgs);
        else if (eventName == "setTime")
            ProcessSetTimeCommand(eventArgs);
        else if (eventName == "launchParty")
            World::Get().GetPartyService().CreateParty();
        else if (eventName == "leaveParty")
            World::Get().GetPartyService().LeaveParty();
        else if (eventName == "createPartyInvite")
        {
            uint32_t aPlayerId = eventArgs->GetInt(0);
            World::Get().GetPartyService().CreateInvite(aPlayerId);
        }
        else if (eventName == "acceptPartyInvite")
        {
            uint32_t aInviterId = eventArgs->GetInt(0);
            // push to main thread because the party service has to check validity of invite thread safely
            World::Get().GetRunner().Queue([aInviterId]() { World::Get().GetPartyService().AcceptInvite(aInviterId); });
        }
        else if (eventName == "kickPartyMember")
        {
            uint32_t aPlayerId = eventArgs->GetInt(0);
            World::Get().GetPartyService().KickPartyMember(aPlayerId);
        }
        else if (eventName == "changePartyLeader")
        {
            uint32_t aPlayerId = eventArgs->GetInt(0);
            World::Get().GetPartyService().ChangePartyLeader(aPlayerId);
        }
        else if (eventName == "teleportToPlayer")
            ProcessTeleportMessage(eventArgs);
        else if (eventName == "toggleDebugUI")
            ProcessToggleDebugUI();

        return true;
    }

    return false;
}

void OverlayClient::ProcessConnectMessage(CefRefPtr<CefListValue> aEventArgs)
{
    std::string baseIp = aEventArgs->GetString(0);
    if (baseIp == "localhost")
    {
        baseIp = "127.0.0.1";
    }

    uint16_t port = aEventArgs->GetInt(1) ? aEventArgs->GetInt(1) : 10578;
    World::Get().GetTransport().SetServerPassword(aEventArgs->GetString(2));

    std::string endpoint = baseIp + ":" + std::to_string(port);

    World::Get().GetRunner().Queue([endpoint] { World::Get().GetTransport().Connect(endpoint); });
}

void OverlayClient::ProcessDisconnectMessage()
{
    World::Get().GetRunner().Queue([]() { World::Get().GetTransport().Close(); });
}

void OverlayClient::ProcessSelectCharacterMessage(CefRefPtr<CefListValue> aEventArgs)
{
    const std::string characterIdValue = aEventArgs->GetString(0).ToString();
    std::uint64_t characterId{};
    if (!TryParseCanonicalCharacterId(characterIdValue, characterId))
    {
        spdlog::warn("Ignoring a character selection request with an invalid character ID encoding.");
        return;
    }

    World::Get().GetRunner().Queue([characterId]() {
        if (!World::Get().GetCharacterSessionService().SelectCharacter(characterId))
            spdlog::debug("Character selection request was rejected by the current client session state.");
    });
}

void OverlayClient::ProcessCreateCharacterMessage(CefRefPtr<CefListValue> aEventArgs)
{
    const int slotIndex = aEventArgs->GetInt(0);
    const std::string name = aEventArgs->GetString(1).ToString();
    if (slotIndex < 0 || slotIndex > 2 || name.size() > 24 * 4)
    {
        spdlog::warn("Ignoring a character creation request with an invalid slot or oversized name.");
        return;
    }

    World::Get().GetDispatcher().trigger(LoadingStageEvent{LoadingStage::kCreatingCharacter, 0.36f});
    World::Get().GetRunner().Queue([slotIndex, name]() {
        if (!World::Get().GetCharacterSessionService().CreateCharacter(static_cast<std::uint32_t>(slotIndex), name))
            spdlog::debug("Character creation request was rejected by the current client session state.");
    });
}

void OverlayClient::ProcessRetryConnectMessage()
{
    m_transport.RetryLauncherSession();
}

void OverlayClient::ProcessQuitGameMessage()
{
    const HWND gameWindow = GetForegroundWindow();
    if (gameWindow)
        PostMessageW(gameWindow, WM_CLOSE, 0, 0);
    else
        PostQuitMessage(0);
}

void OverlayClient::ProcessRevealPlayersMessage()
{
    SetUIVisible(false);
    World::Get().GetMagicService().StartRevealingOtherPlayers();
}

void OverlayClient::ProcessChatMessage(CefRefPtr<CefListValue> aEventArgs)
{
    std::string contents = aEventArgs->GetString(1).ToString();
    if (!contents.empty())
    {
        SendChatMessageRequest messageRequest;
        messageRequest.MessageType = static_cast<ChatMessageType>(aEventArgs->GetInt(0));
        messageRequest.ChatMessage = contents;

        spdlog::info(L"Send chat message of type {}: '{}' ", messageRequest.MessageType, aEventArgs->GetString(1).ToWString());

        m_transport.Send(messageRequest);
    }
}

void OverlayClient::ProcessSetTimeCommand(CefRefPtr<CefListValue> aEventArgs)
{
    const uint8_t hours = static_cast<uint8_t>(aEventArgs->GetInt(0));
    const uint8_t minutes = static_cast<uint8_t>(aEventArgs->GetInt(1));
    const uint32_t senderId = m_transport.GetLocalPlayerId();
    World::Get().GetDispatcher().trigger(SetTimeCommandEvent(hours, minutes, senderId));
}

void OverlayClient::ProcessTeleportMessage(CefRefPtr<CefListValue> aEventArgs)
{
    TeleportRequest request{};
    request.PlayerId = aEventArgs->GetInt(0);

    m_transport.Send(request);
}

void OverlayClient::ProcessToggleDebugUI()
{
    World::Get().GetDebugService().m_showDebugStuff = !World::Get().GetDebugService().m_showDebugStuff;
}

void OverlayClient::SetUIVisible(bool aVisible) noexcept
{
    auto pRenderer = GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    TiltedPhoques::DInputHook::Get().SetEnabled(aVisible);
    World::Get().GetOverlayService().SetActive(aVisible);
    pRenderer->SetCursorVisible(aVisible);
}
