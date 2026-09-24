#pragma once

#include <Structs/GameId.h>

#include <TiltedCore/Stl.hpp>

#include <cstdint>
#include <string_view>

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
    explicit ActorPopulationPolicy(const ESLoader::RecordCollection* apRecordCollection = nullptr);

    void SetRecordCollection(const ESLoader::RecordCollection* apRecordCollection) noexcept;
    void InstallVanillaHumanoidRules();
    void SetRaceClassification(TiltedPhoques::String aRaceEditorId, ActorPopulationClass aClassification);
    // Applies a complete comma-separated list of server-side race editor ID overrides atomically.
    [[nodiscard]] bool ApplyRaceClassificationOverrides(std::string_view aOverrides);

    // Accepts the local/player special case or an already-resolved server form
    // identity. Network GameIds must go through ActorPopulationIdentityResolver.
    [[nodiscard]] ActorPopulationClassification ClassifyActor(const GameId& aActorReference) const noexcept;
    [[nodiscard]] ActorPopulationClassification ClassifyNpcBase(uint32_t aResolvedNpcBaseFormId) const noexcept;

private:
    static constexpr uint32_t kMaxTemplateDepth = 16;

    [[nodiscard]] ActorPopulationClassification ClassifyTemplateTarget(uint32_t aFormId, uint32_t aDepth) const noexcept;

    const ESLoader::RecordCollection* m_recordCollection{};
    TiltedPhoques::Map<TiltedPhoques::String, ActorPopulationClass> m_raceClassifications{};
};
