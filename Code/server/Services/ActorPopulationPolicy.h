#pragma once

#include <Structs/GameId.h>

#include <TiltedCore/Stl.hpp>

#include <cstdint>

namespace ESLoader
{
struct RecordCollection;
}

enum class ActorPopulationClass : uint8_t
{
    kPlayer,
    kHumanoidNpc,
    kCreature,
    kUnknown,
};

struct ActorPopulationClassification
{
    ActorPopulationClass Class = ActorPopulationClass::kUnknown;
    uint32_t NpcFormId{};
    uint32_t RaceFormId{};
    TiltedPhoques::String RaceEditorId{};
};

/**
 * @brief Resolves server-loaded NPC records into configurable population classes.
 *
 * This policy only describes actor data. It does not enforce spawning, assignment, or
 * filtering decisions.
 */
class ActorPopulationPolicy final
{
public:
    explicit ActorPopulationPolicy(const ESLoader::RecordCollection* apRecordCollection = nullptr) noexcept;

    void SetRecordCollection(const ESLoader::RecordCollection* apRecordCollection) noexcept;
    void SetRaceClassification(TiltedPhoques::String aRaceEditorId, ActorPopulationClass aClassification);

    // Accepts the local/player special case or an already-resolved server form
    // identity. Network GameIds must go through ActorPopulationIdentityResolver.
    [[nodiscard]] ActorPopulationClassification ClassifyActor(const GameId& aActorReference) const noexcept;
    [[nodiscard]] ActorPopulationClassification ClassifyNpcBase(uint32_t aResolvedNpcBaseFormId) const noexcept;

private:
    const ESLoader::RecordCollection* m_recordCollection{};
    TiltedPhoques::Map<TiltedPhoques::String, ActorPopulationClass> m_raceClassifications{};
};
