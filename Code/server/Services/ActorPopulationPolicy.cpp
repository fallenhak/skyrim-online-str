#include "ActorPopulationPolicy.h"

#include <RecordCollection.h>
#include <Records/NPC.h>
#include <Records/RACE.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
std::string_view TrimWhitespace(std::string_view aValue) noexcept
{
    while (!aValue.empty() && std::isspace(static_cast<unsigned char>(aValue.front())))
        aValue.remove_prefix(1);

    while (!aValue.empty() && std::isspace(static_cast<unsigned char>(aValue.back())))
        aValue.remove_suffix(1);

    return aValue;
}

bool ParsePopulationClass(std::string_view aValue, ActorPopulationClass& aClassification) noexcept
{
    if (aValue == "HumanoidNpc")
        aClassification = ActorPopulationClass::kHumanoidNpc;
    else if (aValue == "Creature")
        aClassification = ActorPopulationClass::kCreature;
    else if (aValue == "Unknown")
        aClassification = ActorPopulationClass::kUnknown;
    else
        return false;

    return true;
}
} // namespace

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

bool ActorPopulationPolicy::ApplyRaceClassificationOverrides(std::string_view aOverrides)
{
    aOverrides = TrimWhitespace(aOverrides);
    if (aOverrides.empty())
        return true;

    std::vector<std::pair<TiltedPhoques::String, ActorPopulationClass>> parsedOverrides;
    std::size_t entryStart = 0;
    while (entryStart <= aOverrides.size())
    {
        const std::size_t separator = aOverrides.find(',', entryStart);
        const std::size_t entryLength = separator == std::string_view::npos ? aOverrides.size() - entryStart : separator - entryStart;
        const std::string_view entry = TrimWhitespace(aOverrides.substr(entryStart, entryLength));
        if (entry.empty())
            return false;

        const std::size_t equals = entry.find('=');
        if (equals == std::string_view::npos || entry.find('=', equals + 1) != std::string_view::npos)
            return false;

        const std::string_view raceEditorId = TrimWhitespace(entry.substr(0, equals));
        const std::string_view className = TrimWhitespace(entry.substr(equals + 1));
        ActorPopulationClass classification = ActorPopulationClass::kUnknown;
        if (raceEditorId.empty() || !ParsePopulationClass(className, classification))
            return false;

        const auto duplicate = std::find_if(parsedOverrides.begin(), parsedOverrides.end(), [&](const auto& acOverride) {
            return std::string_view(acOverride.first.data(), acOverride.first.size()) == raceEditorId;
        });
        if (duplicate != parsedOverrides.end())
            return false;

        TiltedPhoques::String ownedRaceEditorId;
        ownedRaceEditorId.assign(raceEditorId.data(), raceEditorId.size());
        parsedOverrides.emplace_back(std::move(ownedRaceEditorId), classification);

        if (separator == std::string_view::npos)
            break;
        entryStart = separator + 1;
    }

    for (auto& [raceEditorId, classification] : parsedOverrides)
        SetRaceClassification(std::move(raceEditorId), classification);

    return true;
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
