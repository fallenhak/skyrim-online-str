#include <Services/RenewableEncounterService.h>

#include <World.h>
#include <Game/Player.h>
#include <Components.h>

#include <Events/AcceptedCanonicalCreatureDeathEvent.h>
#include <Events/CharacterExteriorCellChangeEvent.h>
#include <Events/CharacterInteriorCellChangeEvent.h>
#include <Events/CharacterRemoveEvent.h>
#include <Events/CharacterSpawnedEvent.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>

#include <fstream>

#include <Persistence/RenewableEncounterRepository.h>
#include <Services/RenewableEncounterConfig.h>
#include <Services/RenewableEncounterDeathPort.h>
#include <Services/RenewableEncounterSnapshotMapping.h>
#include <Services/RenewableEncounterSpawnBinding.h>

RenewableEncounterService::RenewableEncounterService(World& aWorld, entt::dispatcher& aDispatcher, Persistence::RenewableEncounterRepository& aRepository) noexcept
    : m_world(aWorld)
    , m_repository(aRepository)
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&RenewableEncounterService::OnUpdate>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&RenewableEncounterService::OnPlayerLeave>(this))
    , m_interiorCellChangeConnection(aDispatcher.sink<CharacterInteriorCellChangeEvent>().connect<&RenewableEncounterService::OnInteriorCellChange>(this))
    , m_exteriorCellChangeConnection(aDispatcher.sink<CharacterExteriorCellChangeEvent>().connect<&RenewableEncounterService::OnExteriorCellChange>(this))
    , m_creatureDeathConnection(aDispatcher.sink<AcceptedCanonicalCreatureDeathEvent>().connect<&RenewableEncounterService::OnCreatureDeath>(this))
    , m_characterSpawnedConnection(aDispatcher.sink<CharacterSpawnedEvent>().connect<&RenewableEncounterService::OnCharacterSpawned>(this))
    , m_characterRemoveConnection(aDispatcher.sink<CharacterRemoveEvent>().connect<&RenewableEncounterService::OnCharacterRemove>(this))
{
}

std::filesystem::path RenewableEncounterService::DefaultConfigPath()
{
    return std::filesystem::current_path() / "Data" / "renewable_encounters.txt";
}

void RenewableEncounterService::LoadConfiguration(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath);
    if (!file)
    {
        // A launcher that ships the file to the wrong place would otherwise start the
        // server silently without encounters.
        spdlog::warn("[World] no renewable encounter config at {}, none configured", acPath.string());
        return;
    }

    const auto result = LoadRenewableEncounterConfig(file, m_registry);
    for (const auto& error : result.Errors)
        spdlog::error("[World] {}: {}", acPath.filename().string(), error);

    spdlog::info("[World] renewable encounters loaded encounters={} cells={} slots={} errors={}", result.Encounters, result.Cells, result.Slots, result.Errors.size());
    RestorePersistedState();
}

bool RenewableEncounterService::RestorePersistedState()
{
    const auto restorable = ToRestorableSnapshot(m_registry, m_repository.LoadAll());
    if (restorable.Skipped != 0)
        spdlog::warn("[World] snapshot skipped {} encounter(s) no longer configured", restorable.Skipped);

    if (!m_registry.Restore(restorable.Entries, m_tick))
    {
        spdlog::error("[World] snapshot rejected, encounters start fresh");
        return false;
    }

    spdlog::info("[World] snapshot restored encounters={}", restorable.Entries.size());
    return true;
}

bool RenewableEncounterService::SaveState()
{
    const auto records = ToRenewableEncounterRecords(m_registry.Snapshot(m_tick));
    m_lastSaveTick = m_tick;
    if (!m_repository.SaveAll(records))
    {
        spdlog::error("[World] snapshot save failed encounters={}", records.size());
        return false;
    }

    spdlog::info("[World] snapshot saved encounters={}", records.size());
    return true;
}

void RenewableEncounterService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (acEvent.Delta <= 0.f)
        return;

    m_tickAccumulator += acEvent.Delta;
    while (m_tickAccumulator >= 1.0)
    {
        m_tickAccumulator -= 1.0;
        ++m_tick;
        RunTick();
    }
}

void RenewableEncounterService::RunTick() noexcept
{
    if (const auto expired = m_registry.ExpireSpawnClaims(m_tick, kSpawnClaimTtlTicks))
        spdlog::info("[World] spawn claims expired count={} tick={}", expired, m_tick);

    bool anyReset = false;
    bool anyCooldown = false;
    for (const auto& entry : m_registry.Snapshot(m_tick))
    {
        if (!entry.Cleared)
            continue;

        const auto oldEpoch = entry.Epoch;
        if (m_registry.TryReset(entry.Id, m_tick))
        {
            anyReset = true;
            spdlog::info("[World] encounter reset {:x}/{} epoch={}->{} tick={}", entry.Id.CellFormId, entry.Id.GroupIndex, oldEpoch, oldEpoch + 1, m_tick);
        }
        else if (entry.CooldownRemainingTicks != 0)
            anyCooldown = true;
        else if (m_tick % kResetBlockedLogIntervalTicks == 0
                 && m_registry.GetResetBlocker(entry.Id, m_tick) == RenewableEncounterRegistry::ResetBlocker::Occupied)
            spdlog::info("[World] reset blocked {:x}/{} reason=Occupied tick={}", entry.Id.CellFormId, entry.Id.GroupIndex, m_tick);
    }

    if (anyReset)
        RemoveStaleActors();

    if (anyReset || (anyCooldown && m_tick - m_lastSaveTick >= kCooldownSaveIntervalTicks))
        SaveState();
}

void RenewableEncounterService::RemoveStaleActors() noexcept
{
    // Actors left over from the epoch a reset just retired would otherwise
    // keep sending packets for a slot that now belongs to a new incarnation.
    // Removal goes through CharacterRemoveEvent so CharacterService notifies
    // clients and destroys the entity; the next cell load spawns fresh actors.
    for (const auto serverId : CollectStaleBoundActors(m_registry, m_boundByServerId))
    {
        const auto incarnation = m_boundByServerId[serverId];
        m_boundByServerId.erase(serverId);

        const auto entity = static_cast<entt::entity>(serverId);
        const auto* pLifecycle = m_world.valid(entity) ? m_world.try_get<ActorLifecycleComponent>(entity) : nullptr;
        if (!pLifecycle || pLifecycle->GetGeneration() != incarnation.LifecycleGeneration)
            continue;

        m_world.GetDispatcher().trigger(CharacterRemoveEvent(serverId));
        spdlog::info("[World] stale actor removed incarnation={:x}:{} tick={}", serverId, incarnation.LifecycleGeneration, m_tick);
    }
}

void RenewableEncounterService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (!acEvent.pPlayer)
        return;

    const auto playerId = acEvent.pPlayer->GetId();
    m_registry.RemovePlayer(playerId);
    if (const auto released = m_registry.ReleasePlayerClaims(playerId))
        spdlog::info("[World] spawn claims released count={} player={:x}", released, playerId);
}

void RenewableEncounterService::OnInteriorCellChange(const CharacterInteriorCellChangeEvent& acEvent) noexcept
{
    if (!acEvent.Owner)
        return;

    // Encounter cells are configured as load-order form ids; the network id is
    // resolved against the server's load order, never trusted as-is.
    std::uint32_t cellFormId = 0;
    if (!m_world.ctx().at<ModsComponent>().ResolveServerFormId(acEvent.NewCell, cellFormId))
        cellFormId = 0;

    (void)m_registry.SetPlayerCell(acEvent.Owner->GetId(), cellFormId);
}

void RenewableEncounterService::OnExteriorCellChange(const CharacterExteriorCellChangeEvent& acEvent) noexcept
{
    // Renewable encounters are interior cell sets; being outside occupies none.
    if (acEvent.Owner)
        (void)m_registry.SetPlayerCell(acEvent.Owner->GetId(), 0);
}

void RenewableEncounterService::OnCreatureDeath(const AcceptedCanonicalCreatureDeathEvent& acEvent) noexcept
{
    const auto incarnation = ToEncounterIncarnation(acEvent);
    const auto encounterId = m_registry.FindEncounter(incarnation);
    const auto result = RecordCanonicalCreatureDeath(m_registry, acEvent, m_tick);
    if (result != RenewableEncounterState::DeathResult::Recorded || !encounterId)
        return;

    if (const auto* pEncounter = m_registry.Find(*encounterId); pEncounter && pEncounter->IsCleared())
    {
        spdlog::info("[World] encounter cleared {:x}/{} tick={}", encounterId->CellFormId, encounterId->GroupIndex, m_tick);
        SaveState();
    }
}

void RenewableEncounterService::OnCharacterSpawned(const CharacterSpawnedEvent& acEvent) noexcept
{
    const auto* pFormId = m_world.try_get<FormIdComponent>(acEvent.Entity);
    const auto* pLifecycle = m_world.try_get<ActorLifecycleComponent>(acEvent.Entity);
    const auto* pCharacter = m_world.try_get<CharacterComponent>(acEvent.Entity);
    if (!pFormId || !*pFormId || !pLifecycle || !pLifecycle->IsValid() || !pCharacter || pCharacter->IsPlayer())
        return;

    // Slots are configured as load-order form ids; resolve the placed reference
    // against the server's load order like cell ids.
    std::uint32_t placedRefFormId = 0;
    if (!m_world.ctx().at<ModsComponent>().ResolveServerFormId(pFormId->Id, placedRefFormId))
        return;

    const auto serverId = World::ToInteger(acEvent.Entity);
    const EncounterIncarnation incarnation{serverId, pLifecycle->GetGeneration()};
    switch (BindPlacedActor(m_registry, placedRefFormId, incarnation, pCharacter->IsDead()))
    {
    case PlacedActorBindResult::Bound:
        m_boundByServerId[serverId] = incarnation;
        spdlog::info("[World] spawn completed slot={:x} incarnation={:x}:{} tick={}", placedRefFormId, serverId, incarnation.LifecycleGeneration, m_tick);
        break;
    case PlacedActorBindResult::ArrivedDead:
        spdlog::debug("[World] dead actor not bound slot={:x} incarnation={:x}:{}", placedRefFormId, serverId, incarnation.LifecycleGeneration);
        break;
    case PlacedActorBindResult::Rejected:
        spdlog::warn("[World] spawn rejected slot={:x} incarnation={:x}:{} (slot taken or dead)", placedRefFormId, serverId, incarnation.LifecycleGeneration);
        break;
    case PlacedActorBindResult::NotEncounterSlot:
        break;
    }
}

void RenewableEncounterService::OnCharacterRemove(const CharacterRemoveEvent& acEvent) noexcept
{
    const auto it = m_boundByServerId.find(acEvent.ServerId);
    if (it == m_boundByServerId.end())
        return;

    const auto incarnation = it->second;
    m_boundByServerId.erase(it);
    if (ReleasePlacedActor(m_registry, incarnation))
        spdlog::debug("[World] actor released incarnation={:x}:{}", incarnation.ServerId, incarnation.LifecycleGeneration);
}
