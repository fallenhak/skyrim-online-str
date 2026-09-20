#include <Services/CharacterSaveService.h>

#include <Components.h>
#include <Game/Player.h>
#include <World.h>

#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>
#include <Setting.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <spdlog/spdlog.h>

namespace
{
constexpr std::uint32_t kDefaultAutosaveIntervalSeconds = 30;
constexpr std::uint32_t kMinimumAutosaveIntervalSeconds = 5;
constexpr std::uint32_t kHealthActorValue = 24;
constexpr std::uint32_t kMagickaActorValue = 25;
constexpr std::uint32_t kStaminaActorValue = 26;

Console::Setting uAutosaveIntervalSeconds{
    "Persistence:uAutosaveIntervalSeconds", "Seconds between persistent character runtime saves; 0 disables periodic autosave", kDefaultAutosaveIntervalSeconds};
}

CharacterSaveService::CharacterSaveService(World& aWorld, Persistence::CharacterRepository& aCharacterRepository, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_characterRepository(aCharacterRepository)
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&CharacterSaveService::OnUpdate>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&CharacterSaveService::OnPlayerLeave>(this))
{
}

void CharacterSaveService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    const auto configuredIntervalSeconds = uAutosaveIntervalSeconds.value_as<std::uint32_t>();
    if (configuredIntervalSeconds == 0)
    {
        m_elapsedSeconds = 0.f;
        return;
    }

    if (!std::isfinite(acEvent.Delta) || acEvent.Delta <= 0.f)
        return;

    m_elapsedSeconds += acEvent.Delta;
    if (!std::isfinite(m_elapsedSeconds))
        m_elapsedSeconds = 0.f;

    const auto effectiveIntervalSeconds = std::max(configuredIntervalSeconds, kMinimumAutosaveIntervalSeconds);
    if (m_elapsedSeconds < static_cast<float>(effectiveIntervalSeconds))
        return;

    // Save at most once per update event. A long frame therefore cannot turn into a burst of
    // writes, while the next interval starts from the current tick.
    m_elapsedSeconds = 0.f;

    const auto view = m_world.view<PersistentCharacterComponent, OwnerComponent, CellIdComponent, MovementComponent, ActorValuesComponent, CharacterComponent>();
    for (const auto entity : view)
    {
        if (view.get<CharacterComponent>(entity).IsPlayer())
            (void)SaveEntity(entity, "autosave");
    }
}

void CharacterSaveService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    const Player* const pPlayer = acEvent.pPlayer;
    if (!pPlayer)
        return;

    const auto character = pPlayer->GetCharacter();
    if (!character.has_value() || !m_world.valid(*character))
        return;

    const auto* const pOwnerComponent = m_world.try_get<OwnerComponent>(*character);
    if (!pOwnerComponent || pOwnerComponent->GetOwner() != pPlayer)
    {
        spdlog::warn("[CharacterSave] Refusing disconnect save for player {:x}: character owner mismatch", pPlayer->GetId());
        return;
    }

    // PlayerLeaveEvent fires while the persistent entity still exists. GameServer clears the
    // Player cell and removes the entity only after this event, so read the ECS state here.
    (void)SaveEntity(*character, "disconnect");
}

bool CharacterSaveService::CaptureRuntimeState(const entt::entity aEntity, Persistence::CharacterRuntimeState& aState) const noexcept
{
    const auto* const pPersistentComponent = m_world.try_get<PersistentCharacterComponent>(aEntity);
    const auto* const pCellComponent = m_world.try_get<CellIdComponent>(aEntity);
    const auto* const pMovementComponent = m_world.try_get<MovementComponent>(aEntity);
    const auto* const pActorValuesComponent = m_world.try_get<ActorValuesComponent>(aEntity);
    const auto* const pCharacterComponent = m_world.try_get<CharacterComponent>(aEntity);
    const auto* const pOwnerComponent = m_world.try_get<OwnerComponent>(aEntity);
    if (!pPersistentComponent || !pCellComponent || !pMovementComponent || !pActorValuesComponent || !pCharacterComponent || !pOwnerComponent ||
        !pCharacterComponent->IsPlayer() || !pOwnerComponent->GetOwner())
        return false;

    const auto findCurrentValue = [&pActorValuesComponent](const std::uint32_t aActorValue, float& aOutput) noexcept
    {
        const auto it = pActorValuesComponent->CurrentActorValues.ActorValuesList.find(aActorValue);
        if (it == pActorValuesComponent->CurrentActorValues.ActorValuesList.end())
            return false;

        aOutput = it->second;
        return true;
    };

    aState.WorldSpace = pCellComponent->WorldSpaceId;
    aState.Cell = pCellComponent->Cell;
    aState.PositionX = pMovementComponent->Position.x;
    aState.PositionY = pMovementComponent->Position.y;
    aState.PositionZ = pMovementComponent->Position.z;

    // V1 persists current vitals from the live server ECS state. Actor max/permanent values
    // are intentionally not part of CharacterRuntimeState.
    if (!findCurrentValue(kHealthActorValue, aState.Health) || !findCurrentValue(kMagickaActorValue, aState.Magicka) ||
        !findCurrentValue(kStaminaActorValue, aState.Stamina))
        return false;

    return Persistence::IsValidCharacterRuntimeState(pPersistentComponent->CharacterId, pPersistentComponent->OwnerProfileId, aState);
}

bool CharacterSaveService::SaveEntity(const entt::entity aEntity, const std::string_view acReason) noexcept
{
    const auto* const pPersistentComponent = m_world.try_get<PersistentCharacterComponent>(aEntity);
    if (!pPersistentComponent)
        return false;

    Persistence::CharacterRuntimeState state{};
    if (!CaptureRuntimeState(aEntity, state))
    {
        spdlog::warn("[CharacterSave] Refusing {} save for character {} (entity {:x}): invalid runtime state", acReason, pPersistentComponent->CharacterId,
                     World::ToInteger(aEntity));
        return false;
    }

    try
    {
        if (!m_characterRepository.UpdateCharacterRuntimeState(pPersistentComponent->CharacterId, pPersistentComponent->OwnerProfileId, state))
        {
            spdlog::warn("[CharacterSave] {} save affected no row for character {}", acReason, pPersistentComponent->CharacterId);
            return false;
        }
    }
    catch (const std::exception& acException)
    {
        spdlog::error("[CharacterSave] {} save failed for character {}: {}", acReason, pPersistentComponent->CharacterId, acException.what());
        return false;
    }
    catch (...)
    {
        spdlog::error("[CharacterSave] {} save failed for character {} with an unknown exception", acReason, pPersistentComponent->CharacterId);
        return false;
    }

    return true;
}
