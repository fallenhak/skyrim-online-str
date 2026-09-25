#include <World.h>
#include <Components.h>
#include <ReservedEntity.h>

#include <Services/CharacterService.h>
#include <Services/PresenceService.h>
#include <Services/AuthorityService.h>
#include <Services/ObjectService.h>
#include <Services/QuestService.h>
#include <Services/ServerListService.h>
#include <Services/ActorValueService.h>
#include <Services/AdminService.h>
#include <Services/InventoryService.h>
#include <Services/MagicService.h>
#include <Services/OverlayService.h>
#include <Services/CommandService.h>
#include <Services/StringCacheService.h>
#include <Services/CombatService.h>
#include <Services/WeatherService.h>
#include <Services/ScriptService.h>
#include <Services/MapService.h>
#include <Services/RenewableEncounterService.h>

#include <es_loader/ESLoader.h>

#include <utility>

World::World(
    std::filesystem::path aDatabasePath, bool aEnableActorRecordLoading, bool aEnableHumanoidAssignmentGate, bool aAllowUnknownActorAssignments,
    const char* apRaceClassificationOverrides, const std::uint32_t aCreatureCorpseLifetimeSeconds)
{
    if (const auto reserved = ReserveNullServerEntity(*this); ToInteger(reserved) != 0)
        spdlog::error("Reserved null server entity is {:X}, not 0; id 0 may still reach a player", ToInteger(reserved));

    m_spAdminService = std::make_shared<AdminService>(*this, m_dispatcher);
    spdlog::default_logger()->sinks().push_back(std::static_pointer_cast<spdlog::sinks::sink>(m_spAdminService));

    ctx().emplace<PersistenceService>(std::move(aDatabasePath));
    ctx().emplace<SessionService>(ctx().at<PersistenceService>().GetCharacterRepository());
    ctx().emplace<ProgressionService>(*this, m_dispatcher);
    ctx().emplace<CharacterService>(*this, m_dispatcher, aCreatureCorpseLifetimeSeconds);
    ctx().emplace<CharacterSaveService>(*this, ctx().at<PersistenceService>().GetCharacterRepository(), m_dispatcher);
    ctx().emplace<PlayerService>(*this, m_dispatcher);
    ctx().emplace<PresenceService>(*this, m_dispatcher);
    ctx().emplace<CalendarService>(*this, m_dispatcher);
    ctx().emplace<ObjectService>(*this, m_dispatcher, ctx().at<PersistenceService>().GetWorldObjectRepository());
    ctx().emplace<ModsComponent>();
    ctx().emplace<ServerListService>(*this, m_dispatcher);
    ctx().emplace<QuestService>(*this, m_dispatcher);
    ctx().emplace<PartyService>(*this, m_dispatcher);
    ctx().emplace<AuthorityService>(*this);
    ctx().emplace<ActorValueService>(*this, m_dispatcher);
    ctx().emplace<InventoryService>(*this, m_dispatcher);
    ctx().emplace<MagicService>(*this, m_dispatcher);
    ctx().emplace<OverlayService>(*this, m_dispatcher);
    ctx().emplace<CommandService>(*this, m_dispatcher);
    ctx().emplace<StringCacheService>(*this, m_dispatcher);
    ctx().emplace<CombatService>(*this, m_dispatcher);
    ctx().emplace<WeatherService>(*this, m_dispatcher);
    ctx().emplace<MapService>(*this, m_dispatcher);

    ESLoader::ESLoader loader;
    if (aEnableActorRecordLoading)
        spdlog::info("Actor population record loading enabled; ESLoader will parse server plugins for NPC/race classification.");

    // Load order metadata remains available for ModPolicy. Full plugin parsing is an explicit
    // startup-only opt-in because actor population classification is not runtime filtering.
    m_recordCollection = loader.BuildRecordCollection(aEnableActorRecordLoading);
    if (aEnableActorRecordLoading && m_recordCollection == nullptr)
        spdlog::warn("Actor population classification records are unavailable; NPC classification will remain Unknown.");

    ctx().emplace<ActorPopulationPolicy>(m_recordCollection.get());
    auto& populationPolicy = ctx().at<ActorPopulationPolicy>();
    if (!populationPolicy.ApplyRaceClassificationOverrides(apRaceClassificationOverrides == nullptr ? "" : apRaceClassificationOverrides))
    {
        spdlog::warn(
            "Ignoring invalid Population:sRaceClassificationOverrides; use comma-separated RaceEditorId=HumanoidNpc, Creature, or Unknown entries with no duplicates.");
    }
    auto& modsComponent = ctx().at<ModsComponent>();
    for (const auto& it : loader.GetLoadOrder())
    {
        modsComponent.AddServerMod(it);
    }
    ctx().emplace<ActorPopulationIdentityResolver>(modsComponent, m_recordCollection.get(), populationPolicy);
    ctx().emplace<ActorPopulationAssignmentPolicy>(aEnableHumanoidAssignmentGate, aAllowUnknownActorAssignments);

    // Needs the server load order above to resolve cell ids.
    ctx().emplace<RenewableEncounterService>(*this, m_dispatcher, ctx().at<PersistenceService>().GetRenewableEncounterRepository())
        .LoadConfiguration(RenewableEncounterService::DefaultConfigPath());

    // late initialize the ScriptService to ensure all components are valid
    m_pScriptService = TiltedPhoques::MakeUnique<ScriptService>(*this, m_dispatcher);
}

World::~World()
{
    m_pScriptService.reset();
}
