#pragma once

#include <Persistence/CharacterRepository.h>

#include <entt/entt.hpp>

#include <string_view>

struct Player;
struct PlayerLeaveEvent;
struct UpdateEvent;
struct World;

/**
 * @brief Saves the narrow, server-observed runtime state of persistent player entities.
 *
 * This service deliberately does not persist identity, level, progression, inventory, or
 * appearance. It reads the live ECS entity so disconnect save-back is independent of session
 * teardown and the Player cell component being cleared.
 */
struct CharacterSaveService final
{
    CharacterSaveService(World& aWorld, Persistence::CharacterRepository& aCharacterRepository, entt::dispatcher& aDispatcher) noexcept;
    ~CharacterSaveService() noexcept = default;

    TP_NOCOPYMOVE(CharacterSaveService);

private:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;

    [[nodiscard]] Persistence::CharacterRuntimeStateVerdict CaptureRuntimeState(entt::entity aEntity, Persistence::CharacterRuntimeState& aState) const noexcept;
    [[nodiscard]] bool SaveEntity(entt::entity aEntity, std::string_view acReason) noexcept;

    World& m_world;
    Persistence::CharacterRepository& m_characterRepository;
    float m_elapsedSeconds{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
