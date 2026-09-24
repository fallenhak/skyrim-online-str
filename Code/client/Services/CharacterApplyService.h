#pragma once

#include <Events/CharacterLoadSnapshotReceivedEvent.h>

#include <Structs/CharacterLoadSnapshotValidation.h>

struct World;

/**
 * @brief Validates and applies the server-authoritative V1 character snapshot to Skyrim's local player.
 */
struct CharacterApplyService final
{
    CharacterApplyService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~CharacterApplyService() noexcept = default;

    TP_NOCOPYMOVE(CharacterApplyService);

private:
    void OnCharacterSnapshot(const CharacterLoadSnapshotReceivedEvent& acEvent) noexcept;
    [[nodiscard]] bool ApplySnapshot(const CharacterLoadSnapshot& acSnapshot, TiltedPhoques::String& aFailureReason) const noexcept;
    void Fail(const CharacterLoadSnapshot& acSnapshot, CharacterLoadSnapshotValidationError aError, const char* acReason) const noexcept;

    World& m_world;
    entt::scoped_connection m_snapshotConnection;
};
