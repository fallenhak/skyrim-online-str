#include <Components.h>
#include <GameServer.h>
#include <Packet.hpp>

#include <Events/AdminPacketEvent.h>
#include <Events/CharacterRemoveEvent.h>
#include <Events/OwnershipTransferEvent.h>
#include <Events/PacketEvent.h>
#include <Events/PlayerJoinEvent.h>
#include <Services/AuthTokenVerifier.h>
#include <Events/PlayerLeaveCellEvent.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>
#include <steam/isteamnetworkingutils.h>

#include <AdminMessages/AdminSessionOpen.h>
#include <AdminMessages/ClientAdminMessageFactory.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/ClientMessageFactory.h>
#include <Messages/NotifySettingsChange.h>
#include <Messages/NotifyCharacterList.h>
#include <Messages/NotifyCharacterSelectionResult.h>
#include <Messages/NotifyCharacterLoadSnapshot.h>
#include <Messages/NotifyCharacterReadyResult.h>
#include <Messages/CharacterReadyRequest.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/NotifyCharacterEnteredWorld.h>
#include <Messages/RequestCharacterList.h>
#include <Messages/SelectCharacterRequest.h>
#include <es_loader/PluginFilename.h>
#include <console/ConsoleRegistry.h>
#include <resources/ResourceCollection.h>

#include <limits>
#include <cstdlib>

constexpr size_t kMaxServerNameLength = 128u;

// -- Cvars --
Console::Setting uServerPort{"GameServer:uPort", "Which port to host the server on", 10578u};
Console::Setting uMaxPlayerCount{"GameServer:uMaxPlayerCount", "Maximum number of players allowed on the server (going over the default of 8 is not recommended)", 8u};
Console::Setting bPremiumTickrate{"GameServer:bPremiumMode", "Use premium tick rate", true};

Console::StringSetting sServerName{"GameServer:sServerName", "Name that shows up in the server list", "Dedicated Together Server"};
Console::StringSetting sAdminPassword{"GameServer:sAdminPassword", "Admin authentication password", ""};
Console::StringSetting sPassword{"GameServer:sPassword", "Server password", ""};
Console::StringSetting sPersistenceDatabasePath{"Persistence:sDatabasePath", "SQLite database path relative to the server working directory", "Data/SkyrimTogetherServer.db"};
Console::Setting bEnableActorRecordLoading{
    "Population:bEnableActorRecordLoading",
    "Enable full server plugin record parsing for actor-population NPC/race classification at startup; not runtime filtering",
    false,
    Console::SettingsFlags::kLocked};
Console::Setting bEnableHumanoidAssignmentGate{
    "Population:bEnableHumanoidAssignmentGate",
    "Reject trusted vanilla humanoid NPCs from becoming STR-managed server entities",
    false,
    Console::SettingsFlags::kLocked};
Console::Setting bAllowUnknownActorAssignments{
    "Population:bAllowUnknownActorAssignments",
    "Allow actor assignments when server population identity is unknown or untrusted",
    true,
    Console::SettingsFlags::kLocked};
Console::StringSetting sRaceClassificationOverrides{
    "Population:sRaceClassificationOverrides",
    "Comma-separated server race overrides in RaceEditorId=Class form; Class is HumanoidNpc, Creature, or Unknown; blank keeps the ten defaults",
    "",
    Console::SettingsFlags::kLocked};
Console::Setting bAnnounceServer{"LiveServices:bAnnounceServer", "Whether to list the server on the public server list", false};
Console::Setting bEnableDevelopmentIdentityBinding{
    "Identity:bEnableDevelopmentIdentityBinding", "(Development only) Allow server operators to bind a live player to an explicit owner profile", false, Console::SettingsFlags::kLocked};

// Gameplay
// TODO: to make this easier for users, use game names for difficulty instead of int
Console::Setting uDifficulty{"Gameplay:uDifficulty", "In game difficulty (0 to 5)", 4u};
Console::Setting bEnableGreetings{"Gameplay:bEnableGreetings", "Enables NPC greetings (disabled by default since they can be spammy with dialogue sync)", false};
Console::Setting bEnablePvp{"Gameplay:bEnablePvp", "Enables pvp", false};
Console::Setting bSyncPlayerHomes{"Gameplay:bSyncPlayerHomes", "Sync chests and displays in player homes and other NoResetZones", false};
Console::Setting bEnableDeathSystem{"Gameplay:bEnableDeathSystem", "Enables the custom multiplayer death system", true};
Console::Setting uTimeScale{"Gameplay:uTimeScale", "How many seconds pass ingame for every real second (0 to 1000). Changing this can make the game unstable", 20u};
Console::Setting bSyncPlayerCalendar{"Gameplay:bSyncPlayerCalendar", "Syncs up all player calendars to be the same day, month, and year. This uses the date of the player with the furthest ahead date at connection.", false};
Console::Setting bAutoPartyJoin{"Gameplay:bAutoPartyJoin", "Join parties automatically, as long as there is only one party in the server", true};
// ModPolicy Stuff
Console::Setting bEnableModCheck{"ModPolicy:bEnableModCheck", "Bypass the checking of mods on the server", false, Console::SettingsFlags::kLocked};
Console::Setting bAllowSKSE{"ModPolicy:bAllowSKSE", "Allow clients with SKSE active to join", true, Console::SettingsFlags::kLocked};
Console::Setting bAllowMO2{"ModPolicy:bAllowMO2", "Allow clients running Mod Organizer 2 to join", true, Console::SettingsFlags::kLocked};

// -- Commands --
Console::Command<> TogglePremium(
    "TogglePremium", "Toggle Premium Tickrate on/off",
    [](Console::ArgStack&)
    {
        bPremiumTickrate = !bPremiumTickrate;
        spdlog::get("ConOut")->info("Premium Tickrate has been {}.", bPremiumTickrate == true ? "enabled" : "disabled");
    });

Console::Command<> TogglePvp(
    "TogglePvp", "Toggle PvP on/off",
    [](Console::ArgStack&)
    {
        bEnablePvp = !bEnablePvp;
        spdlog::get("ConOut")->info("PvP has been {}.", bEnablePvp == true ? "enabled" : "disabled");
        GameServer::Get()->UpdateSettings();
    });

Console::Command<int64_t> SetDifficulty(
    "SetDifficulty", "Set server difficulty (0 being Novice and 5 being Legendary; default is 4)",
    [](Console::ArgStack& aStack)
    {
        auto aDiff = aStack.Pop<int64_t>();

        if (aDiff < 0 || aDiff > 5)
        {
            spdlog::warn(
                "Game difficulty is invalid (should be from 0 to 5, "
                "current value is {}), setting difficulty to 4 (master).",
                aDiff);

            aDiff = 4;
        }

        uDifficulty = (uint32_t)aDiff;

        GameServer::Get()->UpdateSettings();
        spdlog::get("ConOut")->info("Difficulty has been set to {}.", aDiff);
    });

Console::Command<> ShowVersion("version", "Show the version the server was compiled with", [](Console::ArgStack&) { spdlog::get("ConOut")->info("Server " BUILD_COMMIT); });

Console::Command<> CrashServer(
    "crash", "Crashes the server, don't use!",
    [](Console::ArgStack&)
    {
        int* i = 0;
        *i = 42;
    });

Console::Command<> ShowMoPoStatus(
    "ShowMOPOStats", "Shows the status of ModPolicy",
    [](Console::ArgStack&)
    {
        auto formatStatus = [](bool aToggle)
        {
            return aToggle ? "yes" : "no";
        };

        spdlog::get("ConOut")->info("Modcheck enabled: {}\nSKSE allowed: {}\nMO2 allowed: {}", formatStatus(bEnableModCheck), formatStatus(bAllowSKSE), formatStatus(bAllowMO2));
    });

// -- Constants --
constexpr char kBypassMoPoWarning[]{"ModCheck is disabled. This can lead to desync and other oddities. Make sure you know what you are doing. We "
                                    "may not be able to assist you if ModCheck was disabled."};

constexpr char kMopoRecordsMissing[]{"Failed to start: ModPolicy's ModCheck is enabled, but no mods are installed. Players won't be able "
                                     "to join! Please create a Data/ directory, and put a \"loadorder.txt\" file in there."
                                     "Check the wiki, which can be found on skyrim-together.com, for more details."};

constexpr char kCalendarSyncWarning[]{"Calendar sync is enabled. We generally do not recommend that you use this feature."
                                      "Calendar sync can cause the calendar to jump ahead or behind, which might mess up the timing of quests."
                                      "If you disable this feature again (which is the default setting), the days will still progress, but the"
                                      "exact date will differ slightly between clients (which has no impact on gameplay)."};

static uint16_t GetUserTickRate()
{
    return bPremiumTickrate ? 60 : 30;
}

static bool IsMoPoActive()
{
    return bEnableModCheck;
}

ServerSettings GetSettings()
{
    ServerSettings settings{};
    settings.Difficulty = uDifficulty.value_as<uint8_t>();
    settings.GreetingsEnabled = bEnableGreetings;
    settings.PvpEnabled = bEnablePvp;
    settings.SyncPlayerHomes = bSyncPlayerHomes;
    settings.DeathSystemEnabled = bEnableDeathSystem;
    settings.SyncPlayerCalendar = bSyncPlayerCalendar;
    settings.AutoPartyJoin = bAutoPartyJoin;
    return settings;
}

GameServer::GameServer(Console::ConsoleRegistry& aConsole)
    : m_lastFrameTime(std::chrono::high_resolution_clock::now())
    , m_startTime(std::chrono::high_resolution_clock::now())
    , m_commands(aConsole)
    , m_requestStop(false)
{
    BASE_ASSERT(s_pInstance == nullptr, "Server instance already exists?");
    s_pInstance = this;

    auto port = uServerPort.value_as<uint16_t>();
    while (!Host(port, GetUserTickRate()))
    {
        spdlog::warn("Port {} is already in use, trying {}", port, port + 1);
        port++;
    }

    if (uDifficulty.value_as<uint8_t>() > 5)
    {
        spdlog::warn(
            "Game difficulty is invalid (should be from 0 to 5, current value is {}), setting difficulty to 4 "
            "(master).",
            uDifficulty.value_as<uint8_t>());

        uDifficulty = 4;
    }

    if (!bEnableDeathSystem)
    {
        spdlog::warn("The multiplayer death system is disabled on this server. We recommend that you ONLY do this if you have"
                     " a mod that replaces the vanilla death system. You should only disable our death system if you"
                     " absolutely know what you are doing!");
    }

    m_isPasswordProtected = strcmp(sPassword.value(), "") != 0;

    UpdateInfo();
    spdlog::info("Server {} started on port {}", BUILD_COMMIT, GetPort());
    UpdateTitle();

    m_pWorld = MakeUnique<World>(std::filesystem::path(sPersistenceDatabasePath.value()), bEnableActorRecordLoading, bEnableHumanoidAssignmentGate,
        bAllowUnknownActorAssignments, sRaceClassificationOverrides.value());

    if (bEnableDevelopmentIdentityBinding)
    {
        spdlog::warn("Development identity binding is enabled. This is for local development only and is not authentication.");
    }

    BindMessageHandlers();
    UpdateTimeScale();

    m_pResources = MakeUnique<Resources::ResourceCollection>();
}

GameServer::~GameServer()
{
    s_pInstance = nullptr;
}

GameServer* GameServer::Get() noexcept
{
    return s_pInstance;
}

bool GameServer::IsPublicServer() const noexcept
{
    return bAnnounceServer;
}

bool GameServer::AllowsAutoPartyJoin() const noexcept
{
    return bAutoPartyJoin && !IsPublicServer();
}

void GameServer::Initialize()
{
    if (!CheckMoPo())
        return;

    if (bSyncPlayerCalendar)
        spdlog::warn(kCalendarSyncWarning);

    BindServerCommands();
    m_pWorld->GetScriptService().Initialize(*m_pResources);
}

void GameServer::Kill()
{
    spdlog::info("Server shutdown requested");
    m_requestStop = true;
}

bool GameServer::CheckMoPo()
{
    if (!bEnableModCheck)
    {
        // TODO: re-enable this warning when mopo has good ui and the line endings problem is fixed
        // spdlog::warn(kBypassMoPoWarning);
    }
    // Server is not aware of any installed mods.
    else if (!m_pWorld->GetRecordCollection())
    {
        spdlog::error(kMopoRecordsMissing);
        Kill();
        return false;
    }
    else
        spdlog::info("ModPolicy is active");
    return true;
}

void GameServer::BindMessageHandlers()
{
    auto handlerGenerator = [this](auto& x)
    {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId)
        {
            // Authentication, character selection, and other pre-world protocol handlers are
            // installed explicitly below. Everything generated here is ordinary gameplay.
            if (!m_pWorld->GetSessionService().CanProcessGameplay(aConnectionId))
                return;

            auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);

            if (!pPlayer)
            {
                spdlog::error("Connection {:x} is not associated with a player.", aConnectionId);
                Kick(aConnectionId);
                return;
            }

            const auto pRealMessage = CastUnique<T>(std::move(apMessage));

            m_pWorld->GetDispatcher().trigger(PacketEvent<T>(pRealMessage.get(), pPlayer));
        };

        return false;
    };

    ClientMessageFactory::Visit(handlerGenerator);

    m_messageHandlers[RequestCharacterList::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId)
    {
        auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);
        if (!pPlayer)
        {
            spdlog::error("Connection {:x} is not associated with a player.", aConnectionId);
            Kick(aConnectionId);
            return;
        }

        auto pRealMessage = CastUnique<RequestCharacterList>(std::move(apMessage));
        (void)pRealMessage;

        const auto characters = m_pWorld->GetSessionService().ListCharacters(aConnectionId);
        if (!characters.has_value())
            return;

        NotifyCharacterList response{};
        response.Characters = *characters;
        pPlayer->Send(response);
    };

    m_messageHandlers[SelectCharacterRequest::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId)
    {
        auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);
        if (!pPlayer)
        {
            spdlog::error("Connection {:x} is not associated with a player.", aConnectionId);
            Kick(aConnectionId);
            return;
        }

        const auto pRealMessage = CastUnique<SelectCharacterRequest>(std::move(apMessage));

        NotifyCharacterSelectionResult response{};
        response.Status = m_pWorld->GetSessionService().SelectCharacter(aConnectionId, pRealMessage->CharacterId);
        if (response.Status == CharacterSelectionStatus::kSuccess)
        {
            const auto snapshot = m_pWorld->GetSessionService().PrepareCharacterLoadSnapshot(aConnectionId);
            if (!snapshot.has_value())
            {
                // Keep the external failure generic and do not report selection success when
                // the selected record disappeared before its snapshot could be prepared.
                response.Status = CharacterSelectionStatus::kNotFoundOrNotOwned;
                pPlayer->Send(response);
                return;
            }

            pPlayer->Send(response);

            NotifyCharacterLoadSnapshot snapshotMessage{};
            snapshotMessage.Snapshot = *snapshot;
            pPlayer->Send(snapshotMessage);
            return;
        }

        pPlayer->Send(response);
    };

    m_messageHandlers[CharacterReadyRequest::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId)
    {
        auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);
        if (!pPlayer)
        {
            spdlog::error("Connection {:x} is not associated with a player.", aConnectionId);
            Kick(aConnectionId);
            return;
        }

        const auto pRealMessage = CastUnique<CharacterReadyRequest>(std::move(apMessage));
        NotifyCharacterReadyResult response{};
        response.Status = m_pWorld->GetSessionService().AcceptCharacterReady(aConnectionId, pRealMessage->CharacterId);
        pPlayer->Send(response);
    };

    // Character assignment is the one pre-world exception. Only the real local player
    // reference may cross this gate while the session is awaiting player assignment.
    m_messageHandlers[AssignCharacterRequest::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId)
    {
        const auto pRealMessage = CastUnique<AssignCharacterRequest>(std::move(apMessage));
        const bool isPlayerReference = pRealMessage->ReferenceId.ModId == 0 && pRealMessage->ReferenceId.BaseId == 0x14;
        const auto& sessionService = m_pWorld->GetSessionService();
        if ((isPlayerReference && !sessionService.CanAssignPlayer(aConnectionId)) || (!isPlayerReference && !sessionService.CanProcessGameplay(aConnectionId)))
            return;

        auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);
        if (!pPlayer)
        {
            spdlog::error("Connection {:x} is not associated with a player.", aConnectionId);
            Kick(aConnectionId);
            return;
        }

        m_pWorld->GetDispatcher().trigger(PacketEvent<AssignCharacterRequest>(pRealMessage.get(), pPlayer));
    };

    // Override authentication request
    m_messageHandlers[AuthenticationRequest::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId)
    {
        const auto pRealMessage = CastUnique<AuthenticationRequest>(std::move(apMessage));
        HandleAuthenticationRequest(aConnectionId, pRealMessage);
    };

    auto adminHandlerGenerator = [this](auto& x)
    {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_adminMessageHandlers[T::Opcode] = [this](UniquePtr<ClientAdminMessage>& apMessage, ConnectionId_t aConnectionId)
        {
            const auto pRealMessage = CastUnique<T>(std::move(apMessage));
            m_pWorld->GetDispatcher().trigger(AdminPacketEvent<T>(pRealMessage.get(), aConnectionId));
        };

        return false;
    };

    ClientAdminMessageFactory::Visit(adminHandlerGenerator);
}

void GameServer::BindServerCommands()
{
    m_commands.RegisterCommand<>(
        "uptime", "Show how long the server has been running for",
        [this](Console::ArgStack&)
        {
            Uptime uptime = GetUptime();
            spdlog::get("ConOut")->info("Server uptime: {}w {}d {}h {}m", uptime.weeks, uptime.days, uptime.hours, uptime.minutes);
        });

    m_commands.RegisterCommand<>(
        "players", "List all players on this server",
        [&](Console::ArgStack&)
        {
            auto out = spdlog::get("ConOut");
            uint32_t count = m_pWorld->GetPlayerManager().Count();
            if (count == 0)
            {
                out->warn("No players on here. Invite some friends!");
                return;
            }

            out->info("<------Players-({})--->", count);
            for (Player* pPlayer : m_pWorld->GetPlayerManager())
            {
                out->info("{}: {}", pPlayer->GetId(), pPlayer->GetUsername().c_str());
            }
        });

    m_commands.RegisterCommand<int64_t, String>(
        "DevBindIdentity", "(Development only) Bind a live PlayerId to an explicit OwnerProfileId",
        [&](Console::ArgStack& aStack)
        {
            auto out = spdlog::get("ConOut");
            const auto playerId = aStack.Pop<int64_t>();
            const auto ownerProfileId = aStack.Pop<String>();

            if (!bEnableDevelopmentIdentityBinding)
            {
                out->error("DevBindIdentity is disabled. Enable Identity:bEnableDevelopmentIdentityBinding explicitly for local development.");
                return;
            }

            if (playerId < 0 || playerId > std::numeric_limits<uint32_t>::max())
            {
                out->error("PlayerId {} is outside the valid range.", playerId);
                return;
            }

            if (ownerProfileId.empty())
            {
                out->error("OwnerProfileId must not be empty.");
                return;
            }

            auto* pPlayer = m_pWorld->GetPlayerManager().GetById(static_cast<uint32_t>(playerId));
            if (!pPlayer)
            {
                out->error("No active player was found for PlayerId {}.", playerId);
                return;
            }

            const auto connectionId = pPlayer->GetConnectionId();
            const auto* pSession = m_pWorld->GetSessionService().Get(connectionId);
            if (!pSession)
            {
                out->error("No session was found for PlayerId {} (connection {:x}).", playerId, connectionId);
                return;
            }

            if (pSession->State != SessionState::kAwaitingIdentity)
            {
                out->error("PlayerId {} is not awaiting identity binding; refusing to overwrite the existing session identity.", playerId);
                return;
            }

            if (!m_pWorld->GetSessionService().BindIdentity(connectionId, ownerProfileId))
            {
                out->error("Identity binding failed for PlayerId {} (connection {:x}).", playerId, connectionId);
                return;
            }

            out->info(
                "Development identity bound: PlayerId {} ('{}', connection {:x}) -> OwnerProfileId '{}'; session is awaiting character selection.",
                pPlayer->GetId(), pPlayer->GetUsername().c_str(), connectionId, ownerProfileId.c_str());
        });

    m_commands.RegisterCommand<>(
        "mods", "List all installed mods on this server",
        [&](Console::ArgStack&)
        {
            auto out = spdlog::get("ConOut");
            auto& mods = m_pWorld->ctx().at<ModsComponent>().GetServerMods();
            if (mods.size() == 0)
            {
                out->warn("No mods installed");
                return;
            }

            out->info("<------Mods-({})--->", mods.size());
            for (auto& it : mods)
            {
                out->info(it.first);
            }
        });

    m_commands.RegisterCommand<>(
        "resources", "List all loaded resources on the server",
        [&](Console::ArgStack&)
        {
            auto out = spdlog::get("ConOut");
            if (!m_pResources || m_pResources->GetManifests().size() == 0)
            {
                out->warn("No resources loaded");
                return;
            }

            out->info("<------Resources-({})--->", m_pResources->GetManifests().size());
            m_pResources->ForEachManifest([&](const auto& aManifest) { out->info("{} -> {}", aManifest.Name.c_str(), aManifest.Description.c_str()); });
        });

    m_commands.RegisterCommand<>("quit", "Stop the server", [&](Console::ArgStack&) { Kill(); });

    m_commands.RegisterCommand<int64_t, int64_t>(
        "SetTime", "Set ingame hour and minute",
        [&](Console::ArgStack& aStack)
        {
            auto out = spdlog::get("ConOut");

            auto hour = aStack.Pop<int64_t>();
            auto minute = aStack.Pop<int64_t>();
            auto timescale = m_pWorld->GetCalendarService().GetTimeScale();

            bool time_set_successfully = m_pWorld->GetCalendarService().SetTime(hour, minute, timescale);

            if (time_set_successfully)
            {
                out->info("Time set to {:02}:{:02}", hour, minute);
            }
            else
            {
                out->error("Hour must be between 0-23 and minute must be between 0-59");
            }
        });

    m_commands.RegisterCommand<int64_t, int64_t, int64_t>(
        "SetDate", "Set ingame day, month, and year",
        [&](Console::ArgStack& aStack)
        {
            auto out = spdlog::get("ConOut");

            auto day = aStack.Pop<int64_t>();
            auto month = aStack.Pop<int64_t>();
            auto year = aStack.Pop<int64_t>();

            bool time_set_successfully = m_pWorld->GetCalendarService().SetDate(day, month, year);

            if (time_set_successfully)
            {
                out->info("Time set to {:02}/{:02}:{:02}", month, day, year);
            }
            else
            {
                out->error("Day must be between 0 and 31, month must be between 0 and 11, and year must be between 0 and 999.");
            }
        });

    m_commands.RegisterCommand<std::string>(
        "AddAdmin", "Add admin privileges to player",
        [&](Console::ArgStack& aStack)
        {
            auto out = spdlog::get("ConOut");

            const auto& cUsername = aStack.Pop<String>();
            if (GetAdminByUsername(cUsername))
            {
                out->info("{} is already an admin", cUsername.c_str());
                return;
            }

            auto* pPlayer = PlayerManager::Get()->GetByUsername(cUsername);
            if (pPlayer)
            {
                AddAdminSession(pPlayer->GetConnectionId());
                out->info("{} admin privileges added", cUsername.c_str());
            }
            else
            {
                // retry after sanitizing username
                String backupUsername = SanitizeUsername(cUsername);
                pPlayer = PlayerManager::Get()->GetByUsername(backupUsername);

                if (pPlayer)
                {
                    AddAdminSession(pPlayer->GetConnectionId());
                    out->info("{} admin privileges added", cUsername.c_str());
                }
                else
                {
                    out->warn("{} is not a valid player", backupUsername.c_str());
                }
            }
        });
    m_commands.RegisterCommand<std::string>(
        "RemoveAdmin", "Remove admin privileges from player",
        [&](Console::ArgStack& aStack)
        {
            auto out = spdlog::get("ConOut");

            const auto& cUsername = aStack.Pop<String>();
            auto* pPlayer = GetAdminByUsername(cUsername);

            if (pPlayer)
            {
                RemoveAdminSession(pPlayer->GetConnectionId());
                out->info("{} admin privileges revoked", cUsername.c_str());
            }
            else
            {
                // retry after sanitizing username
                String backupUsername = SanitizeUsername(cUsername);
                pPlayer = GetAdminByUsername(backupUsername);

                if (pPlayer)
                {
                    RemoveAdminSession(pPlayer->GetConnectionId());
                    out->info("{} admin privileges revoked", cUsername.c_str());
                }
                else
                {
                    out->warn("{} is not an admin", backupUsername.c_str());
                }
            }
        });
    m_commands.RegisterCommand<>(
        "admins", "List all admins",
        [&](Console::ArgStack&)
        {
            auto out = spdlog::get("ConOut");
            if (m_adminSessions.size() == 0)
            {
                out->warn("No admins");
                return;
            }

            String output = "Admins: ";
            bool _first = true;

            for (const auto& cAdminSession : m_adminSessions)
            {
                auto* pPlayer = PlayerManager::Get()->GetByConnectionId(cAdminSession);

                if (!pPlayer)
                {
                    out->error("Admin session not found: {}", cAdminSession);
                    continue;
                }

                const auto& cUsername = pPlayer->GetUsername();

                if (_first)
                {
                    _first = false;
                }
                else
                {
                    output += ", ";
                }
                output += cUsername;
            }

            out->info("{}", output.c_str());
        });
}

/* Update Info fields from user facing CVARS.*/
void GameServer::UpdateInfo()
{
    const String cServerName = sServerName.c_str();

    if (cServerName.length() > kMaxServerNameLength)
    {
        spdlog::error("sServerName is longer than the limit of {} characters/bytes, and has been cut short", kMaxServerNameLength);
        m_info.name = cServerName.substr(0U, kMaxServerNameLength);
    }
    else
    {
        m_info.name = cServerName;
    }

    m_info.desc = "";
    m_info.icon_url = "";
    m_info.tagList = "";
    m_info.tick_rate = GetUserTickRate();
}

void GameServer::UpdateTimeScale()
{
    auto timescale = uTimeScale.value_as<float>();

    bool timescale_set_successfully = m_pWorld->GetCalendarService().SetTimeScale(timescale);

    if (!timescale_set_successfully)
    {
        spdlog::warn("TimeScale is invalid (should be from 0 to 1000, current value is {}), setting TimeScale to 20 (default)", timescale);

        uTimeScale = 20u;
    }
}

void GameServer::OnUpdate()
{
    const auto cNow = std::chrono::high_resolution_clock::now();
    const auto cDelta = cNow - m_lastFrameTime;
    m_lastFrameTime = cNow;

    const auto cDeltaSeconds = std::chrono::duration_cast<std::chrono::duration<float>>(cDelta).count();

    auto& dispatcher = m_pWorld->GetDispatcher();

    dispatcher.trigger(UpdateEvent{cDeltaSeconds});

    if (m_requestStop)
        Close();
}

void GameServer::OnConsume(const void* apData, const uint32_t aSize, const ConnectionId_t aConnectionId)
{
    ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    // TODO: ClientAdminMessageFactory
    /*if (m_adminSessions.contains(aConnectionId)) [[unlikely]]
    {
        const ClientAdminMessageFactory factory;
        auto pMessage = factory.Extract(reader);
        if (!pMessage)
        {
            spdlog::error("Couldn't parse packet from {:x}", aConnectionId);
            return;
        }

        m_adminMessageHandlers[pMessage->GetOpcode()](pMessage, aConnectionId);
    }
    else
    {*/
    const ClientMessageFactory factory;
    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("Couldn't parse packet from {:x}", aConnectionId);
        return;
    }

    m_messageHandlers[pMessage->GetOpcode()](pMessage, aConnectionId);
    //}
}

void GameServer::OnConnection(const ConnectionId_t aHandle)
{
    spdlog::info("Connection received {:x}", aHandle);
    if (!m_pWorld->GetSessionService().Create(aHandle))
        spdlog::warn("Session already exists for connection {:x}", aHandle);
    UpdateTitle();
}

void GameServer::OnDisconnection(const ConnectionId_t aConnectionId, EDisconnectReason aReason)
{
    m_adminSessions.erase(aConnectionId);
    m_pWorld->GetSessionService().Remove(aConnectionId);

    auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);

    spdlog::info("Connection ended {:x} - '{}' disconnected", aConnectionId, (pPlayer != NULL ? pPlayer->GetUsername().c_str() : "NULL"));

    m_pWorld->GetScriptService().HandlePlayerQuit(aConnectionId, aReason);

    if (pPlayer)
    {
        if (const auto& cell = pPlayer->GetCellComponent())
        {
            const auto oldCell = cell.Cell;
            pPlayer->SetCellComponent(CellIdComponent{{}, {}, {}});
            m_pWorld->GetDispatcher().trigger(PlayerLeaveCellEvent(oldCell));
        }

        m_pWorld->GetDispatcher().trigger(PlayerLeaveEvent(pPlayer));

        entt::entity playerCharacter = pPlayer->GetCharacter().value_or(static_cast<entt::entity>(0));

        // Cleanup all entities that we own
        auto ownerView = m_pWorld->view<OwnerComponent>();
        for (auto entity : ownerView)
        {
            if (entity == playerCharacter)
            {
                m_pWorld->GetDispatcher().enqueue(CharacterRemoveEvent(World::ToInteger(entity)));
                continue;
            }

            const auto& [ownerComponent] = ownerView.get(entity);
            if (ownerComponent.GetOwner() == pPlayer)
            {
                m_pWorld->GetDispatcher().enqueue(OwnershipTransferEvent(entity));
            }
        }

        m_pWorld->GetDispatcher().update();

        m_pWorld->GetPlayerManager().Remove(pPlayer);
    }

    UpdateTitle();
}

void GameServer::Send(const ConnectionId_t aConnectionId, const ServerMessage& acServerMessage) const
{
    static thread_local TiltedPhoques::ScratchAllocator s_allocator{1 << 18};

    Buffer buffer(1 << 20);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Skip the first byte as it is used by packet

    acServerMessage.Serialize(writer);

    TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), static_cast<uint32_t>(writer.Size()));
    Server::Send(aConnectionId, &packet);

    s_allocator.Reset();
}

void GameServer::Send(ConnectionId_t aConnectionId, const ServerAdminMessage& acServerMessage) const
{
    static thread_local TiltedPhoques::ScratchAllocator s_allocator{1 << 18};

    Buffer buffer(1 << 20);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Skip the first byte as it is used by packet

    acServerMessage.Serialize(writer);

    TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), static_cast<uint32_t>(writer.Size()));
    Server::Send(aConnectionId, &packet);

    s_allocator.Reset();
}

void GameServer::SendToLoaded(const ServerMessage& acServerMessage) const
{
    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        if (pPlayer->GetCellComponent())
            pPlayer->Send(acServerMessage);
    }
}

void GameServer::SendToPlayers(const ServerMessage& acServerMessage, const Player* apExcludedPlayer) const
{
    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        if (pPlayer != apExcludedPlayer)
            pPlayer->Send(acServerMessage);
    }
}

// NOTE: this doesn't check objects in range, only characters in range.
bool GameServer::SendToPlayersInRange(const ServerMessage& acServerMessage, const entt::entity acOrigin, const Player* apExcludedPlayer) const
{
    if (!m_pWorld->valid(acOrigin))
    {
        spdlog::error("Entity is invalid: {:X}", World::ToInteger(acOrigin));
        return false;
    }

    const auto view = m_pWorld->view<CellIdComponent>();
    const auto it = view.find(acOrigin);

    if (it == view.end())
    {
        spdlog::warn("Cell component not found for entity {:X}", World::ToInteger(acOrigin));
        return false;
    }

    const auto& cellComponent = view.get<CellIdComponent>(*it);

    bool isDragon = false;
    if (const auto* characterComponent = m_pWorld->try_get<CharacterComponent>(acOrigin))
        isDragon = characterComponent->IsDragon();

    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        if (cellComponent.IsInRange(pPlayer->GetCellComponent(), isDragon) && pPlayer != apExcludedPlayer)
            pPlayer->Send(acServerMessage);
    }

    return true;
}

void GameServer::SendToParty(const ServerMessage& acServerMessage, const PartyComponent& acPartyComponent, const Player* apExcludeSender) const
{
    if (!acPartyComponent.JoinedPartyId.has_value())
    {
        spdlog::warn("Party does not exist, canceling broadcast.");
        return;
    }

    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        if (pPlayer == apExcludeSender)
            continue;

        const auto& partyComponent = pPlayer->GetParty();
        if (partyComponent.JoinedPartyId == acPartyComponent.JoinedPartyId)
        {
            pPlayer->Send(acServerMessage);
        }
    }
}

void GameServer::SendToPartyInRange(const ServerMessage& acServerMessage, const PartyComponent& acPartyComponent, const entt::entity acOrigin, const Player* apExcludeSender) const
{
    if (!acPartyComponent.JoinedPartyId.has_value())
    {
        spdlog::warn("Party does not exist, canceling broadcast.");
        return;
    }

    const auto view = m_pWorld->view<CellIdComponent>();
    const auto it = view.find(acOrigin);

    if (it == view.end())
    {
        spdlog::warn("Cell component not found for entity {:X}", World::ToInteger(acOrigin));
        return;
    }

    const auto& cellComponent = view.get<CellIdComponent>(*it);

    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        if (pPlayer == apExcludeSender)
            continue;

        if (!cellComponent.IsInRange(pPlayer->GetCellComponent(), false))
            continue;

        if (pPlayer->GetParty().JoinedPartyId != acPartyComponent.JoinedPartyId)
            continue;

        pPlayer->Send(acServerMessage);
    }
}

static String PrettyPrintModList(const Vector<Mods::Entry>& acMods)
{
    String text;
    for (size_t i = 0; i < acMods.size(); i++)
    {
        text += acMods[i].Filename;
        if (i != (acMods.size() - 1))
            text += ", ";
    }

    return text;
}

bool GameServer::ValidateAuthParams(ConnectionId_t aConnectionId, const UniquePtr<AuthenticationRequest>& acRequest)
{
    return false;
}

void GameServer::HandleAuthenticationRequest(const ConnectionId_t aConnectionId, const UniquePtr<AuthenticationRequest>& acRequest)
{
    const auto info = GetConnectionInfo(aConnectionId);

    char remoteAddress[48]{};
    info.m_addrRemote.ToString(remoteAddress, 48, false);

    AuthenticationResponse serverResponse;
    serverResponse.Version = BUILD_COMMIT;

    using RT = AuthenticationResponse::ResponseType;
    auto sendKick = [&](const RT type)
    {
        serverResponse.Type = type;
        Send(aConnectionId, serverResponse);
        // the previous message is a lingering kick, it still gets delivered.
        Kick(aConnectionId);
    };
#if 1
    // to make our testing life a bit easier.
    if (acRequest->Version != BUILD_COMMIT)
    {
        spdlog::info("New player {:x} '{}' tried to connect with client {} - Version mismatch", aConnectionId, remoteAddress, acRequest->Version.c_str());
        sendKick(RT::kWrongVersion);
        return;
    }
#endif

    Auth::SessionClaims signedIdentity{};
    const bool hasSignedIdentityToken = !acRequest->AuthToken.empty();
    if (hasSignedIdentityToken)
    {
        const char* const hmacSecret = std::getenv("SOS_AUTH_HMAC_SECRET");
        std::string errorKey;
        const bool valid = hmacSecret && Auth::VerifySessionToken(acRequest->AuthToken.c_str(), hmacSecret, signedIdentity, errorKey);
        if (!valid)
        {
            serverResponse.AuthErrorKey = errorKey.empty() ? (hmacSecret ? "auth.token_invalid" : "auth.server_misconfigured") : errorKey;
            spdlog::warn("Rejecting connection {:x}: Discord session token failed server validation ({})", aConnectionId, serverResponse.AuthErrorKey.c_str());
            sendKick(RT::kInvalidAuthToken);
            return;
        }
    }

    if (m_pWorld->GetPlayerManager().Count() >= uMaxPlayerCount.value_as<uint32_t>())
    {
        sendKick(RT::kServerFull);
        return;
    }

    bool skseProblem = !bAllowSKSE && acRequest->SKSEActive;
    bool mo2Problem = !bAllowMO2 && acRequest->MO2Active;

    if (skseProblem || mo2Problem)
    {
        TiltedPhoques::String response;
        if (skseProblem)
            response += "SKSE ";
        if (mo2Problem)
            response += "MO2 ";

        spdlog::info("New player {:x} '{}' tried to connect, but {}{} disallowed - Kicked.", aConnectionId, remoteAddress, response.c_str(), skseProblem && mo2Problem ? "are" : "is");

        serverResponse.SKSEActive = acRequest->SKSEActive;
        serverResponse.MO2Active = acRequest->MO2Active;
        sendKick(RT::kClientModsDisallowed);
        return;
    }

    bool adminPasswordUsed = acRequest->Token == sAdminPassword.value() && !sAdminPassword.empty();

    // check if the proper server password was supplied.
    if (hasSignedIdentityToken || acRequest->Token == sPassword.value() || adminPasswordUsed)
    {
        if (adminPasswordUsed)
        {
            m_adminSessions.insert(aConnectionId);
            spdlog::warn("New admin session for {:x} '{}'", aConnectionId, remoteAddress);
        }

        Mods& responseList = serverResponse.UserMods;
        auto& modsComponent = m_pWorld->ctx().at<ModsComponent>();

        if (IsMoPoActive())
        {
            // mods that exist on the client, but not on the server
            // modscomponent contains a list filled in by the recordcollection
            Mods modsToRemove;

            const auto& userMods = acRequest->UserMods.ModList;
            for (const Mods::Entry& mod : userMods)
            {
                // if the client has more mods than the server..
                if (!modsComponent.IsInstalled(mod.Filename))
                {
                    modsToRemove.ModList.push_back(mod);
                }
            }

            // TODO(Vince): if you have a better to do this than two for loops
            // let me know!
            // Also, for the future, lets think about a mode that allows more than the server installed mods
            // but requires essential mods?

            // mods that may exist on the server, but not on the client
            for (const auto& entry : modsComponent.GetServerMods())
            {
                const auto it = std::find_if(userMods.begin(), userMods.end(), [&](const Mods::Entry& it) {
                    return ESLoader::ArePluginFilenamesEqual(it.Filename, entry.first);
                });

                if (it == userMods.end())
                {
                    Mods::Entry removeEntry;
                    removeEntry.Filename = entry.first;
                    removeEntry.Id = 0;
                    modsToRemove.ModList.push_back(removeEntry);
                }
            }

            if (modsToRemove.ModList.size() > 0)
            {
                String text = PrettyPrintModList(modsToRemove.ModList);
                // "ModPolicy: refusing connection {:x} because essential mods are missing: {}"
                // for future reference ^
                spdlog::info("ModPolicy: refusing connection {:x} because the following mods are installed on the client: {}", aConnectionId, text.c_str());

                serverResponse.UserMods.ModList = std::move(modsToRemove.ModList);
                sendKick(RT::kModsMismatch);
                return;
            }
        }

        // Note: to lower traffic we only send the mod ids the user can fix in order as other ids will lead to a
        // null form id anyway
        Vector<String> playerMods;
        Vector<uint16_t> playerModsIds;

        size_t i = 0;
        for (auto& mod : acRequest->UserMods.ModList)
        {
            const uint32_t id = mod.IsLite ? modsComponent.AddLite(mod.Filename) : modsComponent.AddStandard(mod.Filename);

            Mods::Entry entry;
            entry.Filename = mod.Filename;
            entry.Id = static_cast<uint16_t>(id);
            entry.IsLite = mod.IsLite;

            playerMods.push_back(mod.Filename);
            playerModsIds.push_back(entry.Id);
            responseList.ModList.push_back(entry);
        }

        Player* pPlayer = m_pWorld->GetPlayerManager().Create(aConnectionId);
        pPlayer->SetEndpoint(remoteAddress);
        if (hasSignedIdentityToken)
        {
            pPlayer->SetDiscordId(signedIdentity.DiscordId);
            pPlayer->SetUsername(signedIdentity.DisplayName);
            serverResponse.DisplayName = signedIdentity.DisplayName;
            serverResponse.AvatarUrl = signedIdentity.AvatarUrl;
        }
        else
        {
            pPlayer->SetDiscordId(acRequest->DiscordId);
            pPlayer->SetUsername(std::move(acRequest->Username));
        }
        pPlayer->SetMods(playerMods);
        pPlayer->SetModIds(playerModsIds);
        pPlayer->SetLevel(acRequest->Level);

        // this event is shit, needs to be fixed, i know
        auto [canceled, reason] = m_pWorld->GetScriptService().HandlePlayerJoin(aConnectionId);
        if (canceled)
        {
            spdlog::info("New player {:x} has a been rejected because \"{}\".", aConnectionId, reason.c_str());
            Kick(aConnectionId);
            m_pWorld->GetSessionService().Remove(aConnectionId);
            m_pWorld->GetPlayerManager().Remove(pPlayer);
            return;
        }

        const bool sessionMarkedAuthenticated = m_pWorld->GetSessionService().MarkAuthenticated(aConnectionId);
        if (!sessionMarkedAuthenticated)
        {
            spdlog::warn("Unable to transition session {:x} to identity binding state", aConnectionId);
            if (hasSignedIdentityToken)
            {
                Kick(aConnectionId);
                m_pWorld->GetSessionService().Remove(aConnectionId);
                m_pWorld->GetPlayerManager().Remove(pPlayer);
                return;
            }
        }
        else if (hasSignedIdentityToken)
        {
            const std::string ownerProfileId = "discord:" + std::to_string(signedIdentity.DiscordId);
            if (!m_pWorld->GetSessionService().BindIdentity(aConnectionId, ownerProfileId))
            {
                spdlog::warn("Unable to bind signed Discord identity for connection {:x}", aConnectionId);
                Kick(aConnectionId);
                m_pWorld->GetSessionService().Remove(aConnectionId);
                m_pWorld->GetPlayerManager().Remove(pPlayer);
                return;
            }
        }

        serverResponse.PlayerId = pPlayer->GetId();

        auto modList = PrettyPrintModList(acRequest->UserMods.ModList);
        spdlog::info("New player '{}' [{:x}] connected with {} mods\n\t: {}", pPlayer->GetUsername().c_str(), aConnectionId, acRequest->UserMods.ModList.size(), modList.c_str());

        serverResponse.Settings = GetSettings();

        serverResponse.Type = AuthenticationResponse::ResponseType::kAccepted;
        Send(aConnectionId, serverResponse);

        uint32_t startId = 0;
        auto initStringCache = StringCache::Get().Serialize(startId);

        pPlayer->SetStringCacheId(startId);

        Send(aConnectionId, initStringCache);

        m_pWorld->GetDispatcher().trigger(PlayerJoinEvent(pPlayer, acRequest->WorldSpaceId, acRequest->CellId, acRequest->PlayerTime));
    }
    /*else if (acRequest->Token == sAdminPassword.value() && !sAdminPassword.empty())
    {
        AdminSessionOpen response;
        Send(aConnectionId, response);

        m_adminSessions.insert(aConnectionId);
        spdlog::warn("New admin session for {:x} '{}'", aConnectionId, remoteAddress);
    } */
    else
    {
        spdlog::info("New player {:x} '{}' has a bad password, kicking.", aConnectionId, remoteAddress);
        sendKick(RT::kWrongPassword);
    }
}

void GameServer::UpdateSettings()
{
    NotifySettingsChange notify{};
    notify.Settings = GetSettings();

    SendToPlayers(notify);
}

GameServer::Uptime GameServer::GetUptime() const noexcept
{
    auto duration = std::chrono::high_resolution_clock::now() - m_startTime;
    auto weeks = std::chrono::duration_cast<std::chrono::weeks>(duration);
    duration -= weeks;
    auto days = std::chrono::duration_cast<std::chrono::days>(duration);
    duration -= days;
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    duration -= hours;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    return {weeks.count(), days.count(), hours.count(), minutes.count()};
}

void GameServer::UpdateTitle() const
{
    const auto name = m_info.name.empty() ? "Private server" : m_info.name;
    const char* playerText = GetClientCount() <= 1 ? " player" : " players";

    const auto title = fmt::format("{} - {} {} - {} Ticks - " BUILD_BRANCH "@" BUILD_COMMIT, name.c_str(), GetClientCount(), playerText, GetTickRate());

#if TP_PLATFORM_WINDOWS
    SetConsoleTitleA(title.c_str());
#else
    std::cout << "\033]0;" << title << "\007";
#endif
}

Player* GameServer::GetAdminByUsername(const String& acUsername) const noexcept
{
    for (auto session : m_adminSessions)
    {
        if (auto* pPlayer = PlayerManager::Get()->GetByConnectionId(session))
        {
            if (pPlayer->GetUsername() == acUsername)
                return pPlayer;
        }
    }

    return nullptr;
}

Player const* GameServer::GetAdminByUsername(const String& acUsername) noexcept
{
    for (auto session : m_adminSessions)
    {
        if (auto const* pPlayer = PlayerManager::Get()->GetByConnectionId(session))
        {
            if (pPlayer->GetUsername() == acUsername)
                return pPlayer;
        }
    }

    return nullptr;
}

String GameServer::SanitizeUsername(const String& acUsername) const noexcept
{
    String username = acUsername;

    // space in username handling | "_" -> space
    std::ranges::replace(username, '_', ' ');

    return username;
}
