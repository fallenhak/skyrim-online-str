#pragma once

#include <Services/RenewableEncounterRegistry.h>

#include <cstdint>
#include <filesystem>

struct World;
struct UpdateEvent;
struct PlayerLeaveEvent;
struct CharacterInteriorCellChangeEvent;
struct CharacterExteriorCellChangeEvent;
struct AcceptedCanonicalCreatureDeathEvent;

namespace Persistence
{
struct RenewableEncounterRepository;
}

/**
 * @brief Runs the renewable encounter registry on the server (roadmap W11).
 *
 * Owns the registry and feeds it only from server-side signals: accepted
 * creature deaths from Combat (C11), the server's own cell tracking, player
 * disconnects and the server tick. Persisted state is restored once the
 * encounter configuration is loaded and saved whenever an encounter clears or
 * resets. Ticks are whole seconds of server uptime, the unit
 * RenewableEncounterPolicy cooldowns are written in.
 *
 * Spawning and actor packet gating belong to other lanes; they reach the
 * registry through GetRegistry() (GetSpawnRequests/ClaimSpawn/CompleteSpawn,
 * GetIncarnationStatus).
 */
struct RenewableEncounterService
{
    // An open spawn ticket the owning client never completed is dropped after this long.
    static constexpr std::uint64_t kSpawnClaimTtlTicks = 30;
    // While a cleared encounter's cooldown runs, its remaining time is persisted this often.
    static constexpr std::uint64_t kCooldownSaveIntervalTicks = 60;

    RenewableEncounterService(World& aWorld, entt::dispatcher& aDispatcher, Persistence::RenewableEncounterRepository& aRepository) noexcept;
    ~RenewableEncounterService() noexcept = default;

    TP_NOCOPYMOVE(RenewableEncounterService);

    [[nodiscard]] RenewableEncounterRegistry& GetRegistry() noexcept { return m_registry; }
    [[nodiscard]] const RenewableEncounterRegistry& GetRegistry() const noexcept { return m_registry; }
    [[nodiscard]] std::uint64_t GetTick() const noexcept { return m_tick; }

    [[nodiscard]] static std::filesystem::path DefaultConfigPath();

    /**
     * Loads the encounter configuration, then the persisted state for it. Call
     * once at startup, before any player connects. A missing file means no
     * renewable encounters; bad lines are logged and skipped.
     */
    void LoadConfiguration(const std::filesystem::path& acPath);

    /**
     * Applies the persisted state to the configured encounters. Call once,
     * after the configuration has added every encounter and before any
     * player connects. Returns false if the stored snapshot was rejected; the
     * encounters then start fresh.
     */
    bool RestorePersistedState();

    // Persists the current snapshot; used on clear, reset and shutdown.
    bool SaveState();

private:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    void OnInteriorCellChange(const CharacterInteriorCellChangeEvent& acEvent) noexcept;
    void OnExteriorCellChange(const CharacterExteriorCellChangeEvent& acEvent) noexcept;
    void OnCreatureDeath(const AcceptedCanonicalCreatureDeathEvent& acEvent) noexcept;

    void RunTick() noexcept;

    World& m_world;
    Persistence::RenewableEncounterRepository& m_repository;
    RenewableEncounterRegistry m_registry;

    std::uint64_t m_tick{};
    double m_tickAccumulator{};
    std::uint64_t m_lastSaveTick{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_playerLeaveConnection;
    entt::scoped_connection m_interiorCellChangeConnection;
    entt::scoped_connection m_exteriorCellChangeConnection;
    entt::scoped_connection m_creatureDeathConnection;
};
