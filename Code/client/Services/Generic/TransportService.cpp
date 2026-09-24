
#include <Services/TransportService.h>
#include <Services/OverlayService.h>

#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/LoadingStageEvent.h>
#include <Events/UpdateEvent.h>

#include <Games/References.h>
#include <Games/TES.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESRace.h>

#include <TimeManager.h>

#include <Forms/TESNPC.h>
#include <TiltedOnlinePCH.h>
#include <World.h>

#include <Messages/AuthenticationRequest.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/CharacterReadyRequest.h>
#include <Messages/ServerMessageFactory.h>
#include <Messages/NotifySettingsChange.h>
#include <Packet.hpp>

#include <Structs/CharacterSessionOutboundPolicy.h>

#include <ScriptExtender.h>
#include <Services/DiscordService.h>

#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include <rapidjson/document.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <wincrypt.h>

// #include <imgui_internal.h>

static constexpr wchar_t kMO2DllName[] = L"usvfs_x64.dll";

using TiltedPhoques::Packet;

TransportService::TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&TransportService::HandleUpdate>(this);
    m_settingsChangeConnection = m_dispatcher.sink<NotifySettingsChange>().connect<&TransportService::HandleNotifySettingsChange>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&TransportService::HandleConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&TransportService::HandleDisconnected>(this);

    m_connected = false;

    auto handlerGenerator = [this](auto& x)
    {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
        {
            const auto pRealMessage = TiltedPhoques::CastUnique<T>(std::move(apMessage));
            m_dispatcher.trigger(*pRealMessage);
        };

        return false;
    };

    ServerMessageFactory::Visit(handlerGenerator);

    // Override authentication response
    m_messageHandlers[AuthenticationResponse::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
    {
        const auto pRealMessage = TiltedPhoques::CastUnique<AuthenticationResponse>(std::move(apMessage));
        HandleAuthenticationResponse(*pRealMessage);
    };

    LoadLauncherSessionConfig();
}

void TransportService::LoadLauncherSessionConfig() noexcept
{
    const DWORD pathLength = GetEnvironmentVariableW(L"SOS_AUTH_CONFIG_PATH", nullptr, 0);
    if (!pathLength)
    {
        spdlog::info("[LauncherSession] SOS_AUTH_CONFIG_PATH not set; launcher session disabled");
        return;
    }

    m_launcherConfigPresent = true;
    std::wstring path(pathLength, L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"SOS_AUTH_CONFIG_PATH", path.data(), pathLength);
    if (!copied || copied >= pathLength)
    {
        m_launcherConfigErrorKey = "auth.launcher_config_invalid";
        return;
    }
    path.resize(copied);

    try
    {
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        if (!file)
        {
            m_launcherConfigErrorKey = "auth.launcher_config_missing";
            return;
        }
        std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (json.empty() || json.size() > 16 * 1024)
        {
            m_launcherConfigErrorKey = "auth.launcher_config_invalid";
            return;
        }

        rapidjson::Document document;
        document.Parse(json.c_str());
        if (document.HasParseError() || !document.IsObject() || !document.HasMember("ServerAddress") || !document["ServerAddress"].IsString() ||
            !document.HasMember("ServerPort") || !document["ServerPort"].IsInt() || !document.HasMember("ProtectedToken") || !document["ProtectedToken"].IsString())
        {
            m_launcherConfigErrorKey = "auth.launcher_config_invalid";
            return;
        }

        const std::string host = document["ServerAddress"].GetString();
        const int port = document["ServerPort"].GetInt();
        const std::string protectedToken64 = document["ProtectedToken"].GetString();
        if (host.empty() || host.size() > 255 || host.find_first_of(":/\\ \t\r\n") != std::string::npos || port < 1 || port > 65535 || protectedToken64.empty() || protectedToken64.size() > 16384)
        {
            m_launcherConfigErrorKey = "auth.launcher_config_invalid";
            return;
        }

        std::string protectedToken;
        CryptoPP::StringSource decode(protectedToken64, true, new CryptoPP::Base64Decoder(new CryptoPP::StringSink(protectedToken)));
        DATA_BLOB input{};
        input.cbData = static_cast<DWORD>(protectedToken.size());
        input.pbData = reinterpret_cast<BYTE*>(protectedToken.data());
        DATA_BLOB decrypted{};
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &decrypted))
        {
            m_launcherConfigErrorKey = "auth.launcher_token_unavailable";
            return;
        }
        m_launcherAuthToken.assign(reinterpret_cast<const char*>(decrypted.pbData), decrypted.cbData);
        SecureZeroMemory(decrypted.pbData, decrypted.cbData);
        LocalFree(decrypted.pbData);
        if (m_launcherAuthToken.size() < 32 || m_launcherAuthToken.size() > 8192 || std::count(m_launcherAuthToken.begin(), m_launcherAuthToken.end(), '.') != 2)
        {
            m_launcherAuthToken.clear();
            m_launcherConfigErrorKey = "auth.launcher_token_invalid";
            return;
        }
        m_launcherEndpoint = host + ":" + std::to_string(port);
        spdlog::info("[LauncherSession] config loaded, endpoint {}", m_launcherEndpoint);
    }
    catch (const CryptoPP::Exception&)
    {
        m_launcherConfigErrorKey = "auth.launcher_config_invalid";
    }
    catch (...)
    {
        m_launcherConfigErrorKey = "auth.launcher_config_invalid";
    }
}

void TransportService::StartLauncherSession() noexcept
{
    spdlog::info("[LauncherSession] start requested (configPresent={}, error='{}')", m_launcherConfigPresent, m_launcherConfigErrorKey);
    if (!m_launcherConfigPresent)
        return;
    if (!m_launcherConfigErrorKey.empty())
    {
        m_world.GetOverlayService().EmitAuthState("failed", "", "", m_launcherConfigErrorKey);
        return;
    }

    m_launcherAuthenticated = false;
    m_world.GetOverlayService().EmitAuthState("connecting");
    m_world.GetDispatcher().trigger(LoadingStageEvent{LoadingStage::kConnecting, 0.05f});
    const auto endpoint = m_launcherEndpoint;
    m_world.GetRunner().Queue([this, endpoint]() {
        const bool started = Connect(endpoint);
        spdlog::info("[LauncherSession] Connect({}) started={}", endpoint, started);
    });
}

void TransportService::RetryLauncherSession() noexcept
{
    if (!m_launcherConfigPresent)
    {
        m_world.GetOverlayService().EmitAuthState("failed", "", "", "auth.launcher_config_missing");
        return;
    }
    if (!m_launcherConfigErrorKey.empty())
    {
        m_world.GetOverlayService().EmitAuthState("failed", "", "", m_launcherConfigErrorKey);
        return;
    }

    m_launcherAuthenticated = false;
    m_world.GetOverlayService().EmitAuthState("connecting");
    m_world.GetDispatcher().trigger(LoadingStageEvent{LoadingStage::kConnecting, 0.05f});
    const auto endpoint = m_launcherEndpoint;
    m_world.GetRunner().Queue([this, endpoint]() {
        Close();
        Connect(endpoint);
    });
}

bool TransportService::Send(const ClientMessage& acMessage) const noexcept
{
    static thread_local ScratchAllocator s_allocator(1 << 18);

    struct ScopedReset
    {
        ~ScopedReset() { s_allocator.Reset(); }
    } allocatorGuard;

    if (!CanSendMessage(acMessage))
        return false;

    if (IsConnected())
    {
        ScopedAllocator _{s_allocator};

        Buffer buffer(1 << 16);
        Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8); // Write first byte as packet needs it

        acMessage.Serialize(writer);
        TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());

        Client::Send(&packet);

        return true;
    }

    return false;
}

bool TransportService::CanSendMessage(const ClientMessage& acMessage) const noexcept
{
    const auto clientState = m_world.GetCharacterSessionService().GetState();
    CharacterClientSessionPhase phase = CharacterClientSessionPhase::kDisconnected;
    switch (clientState)
    {
    case ClientCharacterSessionState::kAwaitingCharacterSelection:
        phase = CharacterClientSessionPhase::kAwaitingCharacterSelection;
        break;
    case ClientCharacterSessionState::kApplyingCharacter:
        phase = CharacterClientSessionPhase::kApplyingCharacter;
        break;
    case ClientCharacterSessionState::kAwaitingClientReady:
        phase = CharacterClientSessionPhase::kAwaitingClientReady;
        break;
    case ClientCharacterSessionState::kAwaitingPlayerAssignment:
        phase = CharacterClientSessionPhase::kAwaitingPlayerAssignment;
        break;
    case ClientCharacterSessionState::kInWorld:
        phase = CharacterClientSessionPhase::kInWorld;
        break;
    case ClientCharacterSessionState::kDisconnected:
    case ClientCharacterSessionState::kCharacterSelected:
        break;
    }

    bool isLocalPlayerAssignment = false;
    if (acMessage.GetOpcode() == kAssignCharacterRequest)
    {
        const auto& request = static_cast<const AssignCharacterRequest&>(acMessage);
        isLocalPlayerAssignment = request.ReferenceId.ModId == 0 && request.ReferenceId.BaseId == 0x14;
    }

    return CanSendCharacterProtocolMessage(acMessage.GetOpcode(), phase, isLocalPlayerAssignment);
}

void TransportService::OnConsume(const void* apData, uint32_t aSize)
{
    ServerMessageFactory factory;
    TiltedPhoques::ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("Couldn't parse packet from server");
        return;
    }

    m_messageHandlers[pMessage->GetOpcode()](pMessage);
}

void TransportService::OnConnected()
{
    spdlog::info("[LauncherSession] transport connected");
    if (m_launcherConfigPresent)
    {
        m_world.GetOverlayService().EmitAuthState("authenticating");
        m_world.GetDispatcher().trigger(LoadingStageEvent{LoadingStage::kAuthenticating, 0.15f});
    }

    AuthenticationRequest request{};
    request.Version = BUILD_COMMIT;
    request.SKSEActive = IsScriptExtenderLoaded();
    request.MO2Active = GetModuleHandleW(kMO2DllName);

    request.Token = m_serverPassword;
    m_serverPassword = "";
    request.AuthToken = m_launcherAuthToken;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();

    // null if discord is not active
    // TODO: think about user opt out
    request.DiscordId = m_world.ctx().at<DiscordService>().GetUser().id;
    auto* pNpc = pPlayer ? Cast<TESNPC>(pPlayer->baseForm) : nullptr;
    if (pNpc)
    {
        request.Username = pNpc->fullName.value.AsAscii();
    }
    else
    {
        request.Username = "Some dragon boi";
    }

    auto* const cpModManager = ModManager::Get();

    for (auto* pMod : cpModManager->mods)
    {
        if (!pMod->IsLoaded())
            continue;

        auto& entry = request.UserMods.ModList.emplace_back();
        entry.Id = pMod->GetId();
        entry.IsLite = pMod->IsLite();
        entry.Filename = pMod->filename;
    }

    auto& modSystem = m_world.GetModSystem();
    if (pPlayer)
    {
        if (pNpc)
        {
            if (pNpc->raceForm.race)
                request.RaceFormId = pNpc->raceForm.race->formID;
            request.Sex = (pNpc->actorData.actorBaseFlags & TESActorBaseData::IS_FEMALE) != 0 ? 1 : 0;
        }
        request.Position = pPlayer->position;

        if (const auto* pWorldSpace = pPlayer->GetWorldSpace())
        {
            request.WorldSpaceFormId = pWorldSpace->formID;
            modSystem.GetServerModId(pWorldSpace->formID, request.WorldSpaceId);
        }

        if (pPlayer->parentCell)
        {
            request.CellFormId = pPlayer->parentCell->formID;
            modSystem.GetServerModId(pPlayer->parentCell->formID, request.CellId);
        }

        request.Level = pPlayer->GetLevel();
    }

    auto* pGameTime = TimeData::Get();
    if (pGameTime && pGameTime->TimeScale && pGameTime->GameHour && pGameTime->GameYear && pGameTime->GameMonth && pGameTime->GameDay)
    {
        request.PlayerTime.TimeScale = pGameTime->TimeScale->f;
        request.PlayerTime.Time = pGameTime->GameHour->f;
        request.PlayerTime.Year = pGameTime->GameYear->f;
        request.PlayerTime.Month = pGameTime->GameMonth->f;
        request.PlayerTime.Day = pGameTime->GameDay->f;
    }

    Send(request);
}

void TransportService::OnDisconnected(EDisconnectReason aReason)
{
    m_connected = false;

    spdlog::warn("Disconnected from server {}", aReason);

    m_dispatcher.trigger(DisconnectedEvent());
}

void TransportService::OnUpdate()
{
}

void TransportService::HandleUpdate(const UpdateEvent& acEvent) noexcept
{
    Update();
}

void TransportService::HandleConnected(const ConnectedEvent& acEvent) noexcept
{
    m_localPlayerId = acEvent.PlayerId;
}

void TransportService::HandleDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    m_localPlayerId = NULL;
}

void TransportService::HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept
{
    using AR = AuthenticationResponse::ResponseType;
    if (acMessage.Type == AR::kAccepted)
    {
        m_connected = true;
        m_launcherAuthenticated = m_launcherConfigPresent && !m_launcherAuthToken.empty();

        if (m_launcherAuthenticated)
        {
            m_world.GetOverlayService().EmitAuthState("authenticated", acMessage.DisplayName.c_str(), acMessage.AvatarUrl.c_str());
            m_world.GetDispatcher().trigger(LoadingStageEvent{LoadingStage::kFetchingCharacters, 0.35f});
        }

        m_world.SetServerSettings(acMessage.Settings);

        m_dispatcher.trigger(acMessage.UserMods);
        m_dispatcher.trigger(acMessage.Settings);
        m_dispatcher.trigger(ConnectedEvent(acMessage.PlayerId));
        return; // quit the function here.
    }

    m_launcherAuthenticated = false;
    if (m_launcherConfigPresent)
    {
        const std::string errorKey = acMessage.AuthErrorKey.empty() ? "auth.server_rejected" : acMessage.AuthErrorKey.c_str();
        m_world.GetOverlayService().EmitAuthState("failed", "", "", errorKey);
    }

    // error finding

    TiltedPhoques::String ErrorInfo;

    ErrorInfo = "{";

    switch (acMessage.Type)
    {
    case AR::kWrongVersion:
        ErrorInfo += "\"error\": \"wrong_version\", \"data\": {";
        ErrorInfo += fmt::format("\"expectedVersion\": \"{}\", \"version\": \"{}\"", acMessage.Version, BUILD_COMMIT);
        ErrorInfo += "}";
        break;
    case AR::kModsMismatch:
    {
        ErrorInfo += "\"error\": \"mods_mismatch\", \"data\": {\"mods\": [";
        bool first = true;
        for (const auto& m : acMessage.UserMods.ModList)
        {
            if (!first)
                ErrorInfo += ",";
            ErrorInfo += fmt::format("[\"{}\",\"{}\"]", m.Filename.c_str(), m.Id);
            first = false;
        }
        ErrorInfo += "]}";
        break;
    }
    case AR::kClientModsDisallowed:
    {
        ErrorInfo += "\"error\": \"client_mods_disallowed\", \"data\": { \"mods\": [";
        if (acMessage.SKSEActive)
            ErrorInfo += "\"SKSE\"";
        if (acMessage.MO2Active)
            if (acMessage.SKSEActive)
                ErrorInfo += ",";
        ErrorInfo += "\"MO2\"";
        ErrorInfo += "]}";
        break;
    }
    case AR::kWrongPassword:
    {
        ErrorInfo += "\"error\": \"wrong_password\"";
        break;
    }
    case AR::kServerFull:
    {
        ErrorInfo += "\"error\": \"server_full\"";
        break;
    }
    case AR::kInvalidAuthToken:
        ErrorInfo += "\"error\": \"invalid_auth_token\"";
        break;
    default: ErrorInfo += "\"error\": \"no_reason\""; break;
    }

    ErrorInfo += "}";

    ConnectionErrorEvent errorEvent;
    if (!ErrorInfo.empty())
    {
        spdlog::error(ErrorInfo.c_str());
        errorEvent.ErrorDetail = std::move(ErrorInfo);
    }

    m_dispatcher.trigger(errorEvent);
}

void TransportService::HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept
{
    m_world.SetServerSettings(acMessage.Settings);
    m_dispatcher.trigger(acMessage.Settings);
}
