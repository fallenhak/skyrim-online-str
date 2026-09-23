#include "ActorPopulationPolicy.h"

#include <RecordCollection.h>
#include <Records/NPC.h>
#include <Records/RACE.h>

#include <utility>

ActorPopulationPolicy::ActorPopulationPolicy(const ESLoader::RecordCollection* apRecordCollection)
    : m_recordCollection(apRecordCollection)
{
    InstallVanillaHumanoidRules();
}

void ActorPopulationPolicy::SetRecordCollection(const ESLoader::RecordCollection* apRecordCollection) noexcept
{
    m_recordCollection = apRecordCollection;
}

void ActorPopulationPolicy::InstallVanillaHumanoidRules()
{
    SetRaceClassification("NordRace", ActorPopulationClass::kHumanoidNpc);
    SetRaceClassification("BretonRace", ActorPopulationClass::kHumanoidNpc);
    SetRaceClassification("ImperialRace", ActorPopulationClass::kHumanoidNpc);
    SetRaceClassification("RedguardRace", ActorPopulationClass::kHumanoidNpc);
    SetRaceClassification("HighElfRace", ActorPopulationClass::kHumanoidNpc);
    SetRaceClassification("WoodElfRace", ActorPopulationClass::kHumanoidNpc);
    SetRaceClassification("DarkElfRace", ActorPopulationClass::kHumanoidNpc);
    SetRaceClassification("OrcRace", ActorPopulationClass::kHumanoidNpc);
    SetRaceClassification("ArgonianRace", ActorPopulationClass::kHumanoidNpc);
    SetRaceClassification("KhajiitRace", ActorPopulationClass::kHumanoidNpc);
}

void ActorPopulationPolicy::SetRaceClassification(TiltedPhoques::String aRaceEditorId, ActorPopulationClass aClassification)
{
    if (aRaceEditorId.empty())
        return;

    if (aClassification != ActorPopulationClass::kHumanoidNpc && aClassification != ActorPopulationClass::kCreature)
    {
        m_raceClassifications.erase(aRaceEditorId);
        return;
    }

    m_raceClassifications[std::move(aRaceEditorId)] = aClassification;
}

ActorPopulationClassification ActorPopulationPolicy::ClassifyActor(const GameId& aActorReference) const noexcept
{
    if (aActorReference == GameId(0, 0x14))
    {
        ActorPopulationClassification classification;
        classification.Class = ActorPopulationClass::kPlayer;
        return classification;
    }

    // NPC classification requires a resolved server-side form ID. A non-zero STR mod id is
    // still a client/network identity and must not be treated as an ESLoader form ID here.
    if (aActorReference.ModId != 0)
        return {};

    return ClassifyNpcBase(aActorReference.BaseId);
}

ActorPopulationClassification ActorPopulationPolicy::ClassifyNpcBase(uint32_t aResolvedNpcBaseFormId) const noexcept
{
    ActorPopulationClassification classification;
    classification.NpcFormId = aResolvedNpcBaseFormId;

    // Zero is the Form ID null sentinel used when a referenced prefix cannot
    // be resolved. Never let an invalid record at zero turn that into a class.
    if (aResolvedNpcBaseFormId == 0 || m_recordCollection == nullptr)
        return classification;

    const NPC* const pNpc = m_recordCollection->FindNpcById(aResolvedNpcBaseFormId);
    if (pNpc == nullptr || pNpc->m_raceId == 0)
        return classification;

    classification.RaceFormId = pNpc->m_raceId;

    const RACE* const pRace = m_recordCollection->FindRaceById(pNpc->m_raceId);
    if (pRace == nullptr || pRace->m_editorId.empty())
        return classification;

    classification.RaceEditorId = pRace->m_editorId;

    const auto it = m_raceClassifications.find(pRace->m_editorId);
    if (it == m_raceClassifications.end())
        return classification;

    classification.Class = it->second;
    return classification;
}
