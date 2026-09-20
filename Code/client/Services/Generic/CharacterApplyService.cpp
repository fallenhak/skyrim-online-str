#include <TiltedOnlinePCH.h>

#include <Services/CharacterApplyService.h>

#include <Events/CharacterSnapshotAppliedEvent.h>
#include <Events/CharacterSnapshotApplyFailedEvent.h>

#include <Forms/TESNPC.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESRace.h>
#include <Forms/TESWorldSpace.h>
#include <Games/Primitives.h>
#include <Forms/ActorValueInfo.h>
#include <PlayerCharacter.h>

#include <Structs/GridCellCoords.h>

#include <Systems/ModSystem.h>
#include <World.h>

CharacterApplyService::CharacterApplyService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_snapshotConnection(aDispatcher.sink<CharacterLoadSnapshotReceivedEvent>().connect<&CharacterApplyService::OnCharacterSnapshot>(this))
{
}

void CharacterApplyService::OnCharacterSnapshot(const CharacterLoadSnapshotReceivedEvent& acEvent) noexcept
{
    TiltedPhoques::String failureReason;
    if (!ApplySnapshot(acEvent.Snapshot, failureReason))
    {
        const auto error = ValidateCharacterLoadSnapshot(acEvent.Snapshot);
        Fail(acEvent.Snapshot, error == CharacterLoadSnapshotValidationError::kNone ? CharacterLoadSnapshotValidationError::kInvalidCell : error, failureReason.c_str());
        return;
    }

    m_world.GetDispatcher().trigger(CharacterSnapshotAppliedEvent{acEvent.Snapshot});
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
    pPlayer->SetActorValue(ActorValueInfo::kHealth, acSnapshot.Health);
    pPlayer->SetActorValue(ActorValueInfo::kMagicka, acSnapshot.Magicka);
    pPlayer->SetActorValue(ActorValueInfo::kStamina, acSnapshot.Stamina);

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
