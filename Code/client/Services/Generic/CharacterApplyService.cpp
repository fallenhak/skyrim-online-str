#include <TiltedOnlinePCH.h>

#include <Services/CharacterApplyService.h>

#include <Events/CharacterSnapshotAppliedEvent.h>
#include <Events/CharacterSnapshotApplyFailedEvent.h>
#include <Events/CharacterWorldSyncStartedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/LoadingStageEvent.h>
#include <Events/UpdateEvent.h>

#include <Games/Skyrim/Interface/ConsoleCommand.h>
#include <Games/Skyrim/Interface/UI.h>
#include <DInputHook.hpp>
#include <Forms/TESNPC.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESRace.h>
#include <Forms/TESWorldSpace.h>
#include <Games/Primitives.h>
#include <Forms/ActorValueInfo.h>
#include <misc/ActorValueOwner.h>
#include <PlayerCharacter.h>
#include <Services/CharacterSessionService.h>
#include <Services/OverlayService.h>

#include <Structs/GridCellCoords.h>

#include <algorithm>
#include <cmath>

#include <Systems/ModSystem.h>
#include <World.h>

CharacterApplyService::CharacterApplyService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_snapshotConnection(aDispatcher.sink<CharacterLoadSnapshotReceivedEvent>().connect<&CharacterApplyService::OnCharacterSnapshot>(this))
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&CharacterApplyService::OnUpdate>(this))
    , m_worldSyncConnection(aDispatcher.sink<CharacterWorldSyncStartedEvent>().connect<&CharacterApplyService::OnWorldSyncStarted>(this))
    , m_disconnectedConnection(aDispatcher.sink<DisconnectedEvent>().connect<&CharacterApplyService::OnDisconnected>(this))
{
}

void CharacterApplyService::OnCharacterSnapshot(const CharacterLoadSnapshotReceivedEvent& acEvent) noexcept
{
    m_pendingSnapshot = acEvent.Snapshot;
    m_raceMenuWasOpen = false;

    const auto validationError = ValidateCharacterLoadSnapshot(acEvent.Snapshot);
    if (validationError != CharacterLoadSnapshotValidationError::kNone)
    {
        Fail(acEvent.Snapshot, validationError, "snapshot failed basic validation");
        m_pendingSnapshot.reset();
        m_entryPhase = EntryPhase::kIdle;
        return;
    }

    EmitLoadingStage(LoadingStage::kLoadingWorld, 0.38f);

    PlayerCharacter* const pPlayer = PlayerCharacter::Get();
    if (IsMainMenuOpen() || !pPlayer || !pPlayer->parentCell)
    {
        m_entryPhase = EntryPhase::kOpeningCellConsole;
        if (!ConsoleCommand::QueueConsole(UIMessage::kShow))
            spdlog::error("Could not queue Skyrim console to start the save-free world entry.");
        return;
    }

    ApplyPendingSnapshot();
}

void CharacterApplyService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_pendingSnapshot.has_value())
        return;

    static BSFixedString s_consoleMenu("Console");
    static BSFixedString s_raceMenu("RaceSex Menu");
    static BSFixedString s_loadingMenu("Loading Menu");
    const auto* pUi = UI::Get();

    switch (m_entryPhase)
    {
    case EntryPhase::kIdle:
        break;
    case EntryPhase::kOpeningCellConsole:
        if (pUi && pUi->GetMenuOpen(s_consoleMenu))
        {
            if (ConsoleCommand::Execute("coc WhiterunTempleofKynareth"))
                m_entryPhase = EntryPhase::kWaitingForCell;
        }
        else
        {
            ConsoleCommand::QueueConsole(UIMessage::kShow);
        }
        break;
    case EntryPhase::kWaitingForCell:
    {
        auto* pPlayer = PlayerCharacter::Get();
        if (!pUi || !pPlayer || !pPlayer->parentCell || pUi->GetMenuOpen(s_loadingMenu))
            break;

        const auto templeFormId = m_world.GetModSystem().GetGameId(GameId(0, 0x000165A7));
        if (pPlayer->parentCell->formID != templeFormId)
            break;

        if (m_pendingSnapshot->NeedsRaceMenu && !m_pendingSnapshot->PositionX && !m_pendingSnapshot->PositionY && !m_pendingSnapshot->PositionZ)
        {
            NiPoint3 position{};
            NiPoint3 rotation{};
            pPlayer->parentCell->GetCOCPlacementInfo(&position, &rotation, true);
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            {
                Fail(*m_pendingSnapshot, CharacterLoadSnapshotValidationError::kInvalidPosition, "temple COC marker returned a non-finite position");
                m_entryPhase = EntryPhase::kIdle;
                return;
            }

            m_pendingSnapshot->PositionX = position.x;
            m_pendingSnapshot->PositionY = position.y;
            m_pendingSnapshot->PositionZ = position.z;
        }

        ConsoleCommand::QueueConsole(UIMessage::kHide);
        m_entryPhase = EntryPhase::kIdle;
        ApplyPendingSnapshot();
        break;
    }
    case EntryPhase::kOpeningRaceMenuConsole:
        if (pUi && pUi->GetMenuOpen(s_consoleMenu))
        {
            if (ConsoleCommand::Execute("showracemenu"))
                m_entryPhase = EntryPhase::kWaitingForRaceMenu;
        }
        else
        {
            ConsoleCommand::QueueConsole(UIMessage::kShow);
        }
        break;
    case EntryPhase::kWaitingForRaceMenu:
        if (pUi && pUi->GetMenuOpen(s_raceMenu))
        {
            m_raceMenuWasOpen = true;
            ConsoleCommand::QueueConsole(UIMessage::kHide);
            m_entryPhase = EntryPhase::kWaitingForRaceMenuClose;
        }
        break;
    case EntryPhase::kWaitingForRaceMenuClose:
        if (m_raceMenuWasOpen && pUi && !pUi->GetMenuOpen(s_raceMenu))
            FinishRaceMenu();
        break;
    }
}

void CharacterApplyService::OnWorldSyncStarted(const CharacterWorldSyncStartedEvent&) noexcept
{
    if (!m_pendingSnapshot.has_value())
        return;

    if (!m_pendingSnapshot->NeedsRaceMenu)
    {
        EmitLoadingStage(LoadingStage::kEnteringWorld, 0.9f);
        EmitLoadingStage(LoadingStage::kDone, 1.f);
        m_pendingSnapshot.reset();
        return;
    }

    EmitLoadingStage(LoadingStage::kRaceMenu, 0.82f);
    m_world.GetOverlayService().SetActive(false);
    TiltedPhoques::DInputHook::Get().SetEnabled(false);
    m_entryPhase = EntryPhase::kOpeningRaceMenuConsole;
    ConsoleCommand::QueueConsole(UIMessage::kShow);
}

void CharacterApplyService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_pendingSnapshot.reset();
    m_entryPhase = EntryPhase::kIdle;
    m_raceMenuWasOpen = false;
}

void CharacterApplyService::EmitLoadingStage(const LoadingStage aStage, const float aProgress) const noexcept
{
    m_world.GetDispatcher().trigger(LoadingStageEvent{aStage, std::clamp(aProgress, 0.f, 1.f)});
}

bool CharacterApplyService::IsMainMenuOpen() const noexcept
{
    static BSFixedString s_mainMenu("Main Menu");
    const auto* pUi = UI::Get();
    return pUi && pUi->GetMenuOpen(s_mainMenu);
}

void CharacterApplyService::ApplyPendingSnapshot() noexcept
{
    if (!m_pendingSnapshot.has_value())
        return;

    EmitLoadingStage(LoadingStage::kApplyingCharacter, 0.66f);
    TiltedPhoques::String failureReason;
    if (!ApplySnapshot(*m_pendingSnapshot, failureReason))
    {
        const auto error = ValidateCharacterLoadSnapshot(*m_pendingSnapshot);
        Fail(*m_pendingSnapshot, error == CharacterLoadSnapshotValidationError::kNone ? CharacterLoadSnapshotValidationError::kInvalidCell : error, failureReason.c_str());
        m_pendingSnapshot.reset();
        m_entryPhase = EntryPhase::kIdle;
        return;
    }

    m_world.GetDispatcher().trigger(CharacterSnapshotAppliedEvent{*m_pendingSnapshot});
}

bool CharacterApplyService::ApplySnapshot(const CharacterLoadSnapshot& acSnapshot, TiltedPhoques::String& aFailureReason) const noexcept
{
    const auto validationError = ValidateCharacterLoadSnapshot(acSnapshot);
    if (validationError != CharacterLoadSnapshotValidationError::kNone)
    {
        aFailureReason = "snapshot failed basic validation";
        return false;
    }

    PlayerCharacter* const pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
    {
        aFailureReason = "local PlayerCharacter is unavailable";
        return false;
    }

    TESNPC* const pNpc = Cast<TESNPC>(pPlayer->baseForm);
    if (!pNpc)
    {
        aFailureReason = "local PlayerCharacter NPC base is unavailable";
        return false;
    }

    auto& modSystem = m_world.GetModSystem();
    const uint32_t raceId = modSystem.GetGameId(acSnapshot.Race);
    TESRace* const pRace = Cast<TESRace>(TESForm::GetById(raceId));
    if (!pRace)
    {
        aFailureReason = "persistent race could not be resolved through ModSystem";
        return false;
    }

    const uint32_t cellId = modSystem.GetGameId(acSnapshot.CellId);
    TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(cellId));

    TESWorldSpace* pWorldSpace = nullptr;
    if (acSnapshot.WorldSpaceId)
    {
        const uint32_t worldSpaceId = modSystem.GetGameId(acSnapshot.WorldSpaceId);
        pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(worldSpaceId));
        if (!pWorldSpace)
        {
            aFailureReason = "persistent worldspace could not be resolved through ModSystem";
            return false;
        }
    }

    if (!pCell && pWorldSpace)
    {
        const auto coordinates = GridCellCoords::CalculateGridCellCoords(acSnapshot.PositionX, acSnapshot.PositionY);
        pCell = pWorldSpace->LoadCell(coordinates.X, coordinates.Y);
    }

    if (!pCell)
    {
        aFailureReason = "persistent cell could not be resolved or loaded";
        return false;
    }

    if (pWorldSpace && pCell->worldspace && pCell->worldspace != pWorldSpace)
    {
        aFailureReason = "persistent cell and worldspace are inconsistent";
        return false;
    }

    if (!pWorldSpace && pCell->worldspace)
    {
        aFailureReason = "persistent exterior cell is missing its worldspace";
        return false;
    }

    // All database values and all game-form lookups have succeeded. Mutate Skyrim only now.
    pNpc->fullName.value.Set(acSnapshot.Name.c_str());
    pNpc->raceForm.race = pRace;
    pPlayer->race = pRace;

    if (acSnapshot.Sex == 1)
        pNpc->actorData.actorBaseFlags |= TESActorBaseData::IS_FEMALE;
    else
        pNpc->actorData.actorBaseFlags &= ~TESActorBaseData::IS_FEMALE;

    pPlayer->SetLevelMod(static_cast<uint32_t>(acSnapshot.Level));

    // V1 persists current vitals only. ForceActorValue routes through ForceCurrent so these
    // values do not redefine the actor's base, permanent, or maximum values.
    pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, acSnapshot.Health);
    pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kMagicka, acSnapshot.Magicka);
    pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kStamina, acSnapshot.Stamina);

    NiPoint3 position;
    position.x = acSnapshot.PositionX;
    position.y = acSnapshot.PositionY;
    position.z = acSnapshot.PositionZ;
    pPlayer->MoveTo(pCell, position);
    pPlayer->QueueUpdate();
    return true;
}

void CharacterApplyService::Fail(const CharacterLoadSnapshot& acSnapshot, const CharacterLoadSnapshotValidationError aError, const char* acReason) const noexcept
{
    spdlog::error("Failed to apply persistent character {}: {}", acSnapshot.CharacterId, acReason);
    m_world.GetDispatcher().trigger(CharacterSnapshotApplyFailedEvent{aError, TiltedPhoques::String(acReason)});
}

void CharacterApplyService::FinishRaceMenu() noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    auto* pNpc = pPlayer ? Cast<TESNPC>(pPlayer->baseForm) : nullptr;
    auto* pRace = pNpc ? pNpc->raceForm.race : nullptr;
    if (!m_pendingSnapshot || !pNpc || !pRace)
    {
        spdlog::error("RaceMenu closed without a readable local player race.");
        return;
    }

    pNpc->fullName.value.Set(m_pendingSnapshot->Name.c_str());
    const std::int32_t sex = (pNpc->actorData.actorBaseFlags & TESActorBaseData::IS_FEMALE) ? 1 : 0;
    GameId serverRace{};
    if (!m_world.GetModSystem().GetServerModId(pRace->formID, serverRace) ||
        !m_world.GetCharacterSessionService().UpdateCharacterAppearance(serverRace, sex))
    {
        spdlog::error("Could not persist the RaceMenu race and sex for character {}.", m_pendingSnapshot->CharacterId);
    }

    TiltedPhoques::DInputHook::Get().SetEnabled(true);
    m_world.GetOverlayService().SetActive(true);
    EmitLoadingStage(LoadingStage::kEnteringWorld, 0.9f);
    EmitLoadingStage(LoadingStage::kDone, 1.f);
    m_pendingSnapshot.reset();
    m_entryPhase = EntryPhase::kIdle;
    m_raceMenuWasOpen = false;
}
