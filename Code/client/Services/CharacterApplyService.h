#pragma once

#include <Events/CharacterLoadSnapshotReceivedEvent.h>
#include <Events/CharacterWorldSyncStartedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/LoadingStageEvent.h>
#include <Events/UpdateEvent.h>

#include <Structs/CharacterLoadSnapshotValidation.h>

#include <optional>

struct World;

/**
 * @brief Validates and applies the server-authoritative V1 character snapshot to Skyrim's local player.
 */
struct CharacterApplyService final
{
    CharacterApplyService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~CharacterApplyService() noexcept = default;

    TP_NOCOPYMOVE(CharacterApplyService);

    // The title-menu entry flow runs before Papyrus ticks, so World::UpdateNetworkOnly drives it directly.
    void UpdateWithoutVm(double aDelta) noexcept { OnUpdate(UpdateEvent(aDelta)); }

private:
    enum class EntryPhase : std::uint8_t
    {
        kIdle,
        kOpeningCellConsole,
        kWaitingForCell,
        kOpeningRaceMenuConsole,
        kWaitingForRaceMenu,
        kWaitingForRaceMenuClose
    };

    void OnCharacterSnapshot(const CharacterLoadSnapshotReceivedEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnWorldSyncStarted(const CharacterWorldSyncStartedEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    [[nodiscard]] bool ApplySnapshot(const CharacterLoadSnapshot& acSnapshot, TiltedPhoques::String& aFailureReason) const noexcept;
    void Fail(const CharacterLoadSnapshot& acSnapshot, CharacterLoadSnapshotValidationError aError, const char* acReason) const noexcept;
    void EmitLoadingStage(LoadingStage aStage, float aProgress) const noexcept;
    [[nodiscard]] bool IsMainMenuOpen() const noexcept;
    void ApplyPendingSnapshot() noexcept;
    void FinishRaceMenu() noexcept;

    World& m_world;
    std::optional<CharacterLoadSnapshot> m_pendingSnapshot;
    EntryPhase m_entryPhase{EntryPhase::kIdle};
    bool m_raceMenuWasOpen{};
    entt::scoped_connection m_snapshotConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_worldSyncConnection;
    entt::scoped_connection m_disconnectedConnection;
};
