#pragma once

#include "Services/AdminService.h"

#include <Services/PlayerService.h>
#include <Services/PresenceService.h>
#include <Services/PartyService.h>
#include <Services/AuthorityService.h>
#include <Persistence/PersistenceService.h>
#include <Services/CharacterService.h>
#include <Services/CalendarService.h>
#include <Services/QuestService.h>
#include <Services/ScriptService.h>

#include "Game/PlayerManager.h"

#include <filesystem>

namespace ESLoader
{
struct RecordCollection;
}

struct World : entt::registry
{
    explicit World(std::filesystem::path aDatabasePath = PersistenceService::DefaultDatabasePath());
    ~World() noexcept;

    TP_NOCOPYMOVE(World);

    entt::dispatcher& GetDispatcher() noexcept { return m_dispatcher; }
    const entt::dispatcher& GetDispatcher() const noexcept { return m_dispatcher; }
    CharacterService& GetCharacterService() noexcept { return ctx().at<CharacterService>(); }
    const CharacterService& GetCharacterService() const noexcept { return ctx().at<const CharacterService>(); }
    PlayerService& GetPlayerService() noexcept { return ctx().at<PlayerService>(); }
    const PlayerService& GetPlayerService() const noexcept { return ctx().at<const PlayerService>(); }
    PresenceService& GetPresenceService() noexcept { return ctx().at<PresenceService>(); }
    const PresenceService& GetPresenceService() const noexcept { return ctx().at<const PresenceService>(); }
    PartyService& GetPartyService() noexcept { return ctx().at<PartyService>(); }
    const PartyService& GetPartyService() const noexcept { return ctx().at<const PartyService>(); }
    AuthorityService& GetAuthorityService() noexcept { return ctx().at<AuthorityService>(); }
    const AuthorityService& GetAuthorityService() const noexcept { return ctx().at<const AuthorityService>(); }
    PersistenceService& GetPersistenceService() noexcept { return ctx().at<PersistenceService>(); }
    const PersistenceService& GetPersistenceService() const noexcept { return ctx().at<const PersistenceService>(); }
    CalendarService& GetCalendarService() noexcept { return ctx().at<CalendarService>(); }
    const CalendarService& GetCalendarService() const noexcept { return ctx().at<const CalendarService>(); }
    QuestService& GetQuestService() noexcept { return ctx().at<QuestService>(); }
    const QuestService& GetQuestService() const noexcept { return ctx().at<const QuestService>(); }
    PlayerManager& GetPlayerManager() noexcept { return m_playerManager; }
    const PlayerManager& GetPlayerManager() const noexcept { return m_playerManager; }
    ScriptService& GetScriptService() const noexcept { return *m_pScriptService; }

    // Null checked at start when MoPo is on!
    ESLoader::RecordCollection* GetRecordCollection() noexcept { return m_recordCollection.get(); }

    const ESLoader::RecordCollection* GetRecordCollection() const noexcept { return m_recordCollection.get(); }

    [[nodiscard]] static uint32_t ToInteger(entt::entity aEntity) { return to_integral(aEntity); }

private:
    entt::dispatcher m_dispatcher;

    TiltedPhoques::SharedPtr<AdminService> m_spAdminService;
    TiltedPhoques::UniquePtr<ScriptService> m_pScriptService;
    PlayerManager m_playerManager;
    UniquePtr<ESLoader::RecordCollection> m_recordCollection;
};
