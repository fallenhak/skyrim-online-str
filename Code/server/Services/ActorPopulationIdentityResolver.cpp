#include "ActorPopulationIdentityResolver.h"

#define TP_INTERNAL_COMPONENTS_GUARD
#include <Components/ModsComponent.h>
#undef TP_INTERNAL_COMPONENTS_GUARD
#include <RecordCollection.h>
#include <Records/ACHR.h>

#include <limits>

const char* GetActorPopulationIdentitySourceName(const ActorPopulationIdentitySource aSource) noexcept
{
    switch (aSource)
    {
    case ActorPopulationIdentitySource::kPlayer: return "Player";
    case ActorPopulationIdentitySource::kServerPlacedReference: return "ServerPlacedReference";
    case ActorPopulationIdentitySource::kServerNpcBase: return "ServerNpcBase";
    case ActorPopulationIdentitySource::kClientClaimedTemporaryBase: return "ClientClaimedTemporaryBase";
    case ActorPopulationIdentitySource::kClientClaimedLeveledPick: return "ClientClaimedLeveledPick";
    case ActorPopulationIdentitySource::kUnknown: break;
    }

    return "Unknown";
}

ActorPopulationIdentityResolver::ActorPopulationIdentityResolver(
    const ModsComponent& acMods, const ESLoader::RecordCollection* apRecordCollection, const ActorPopulationPolicy& acPolicy) noexcept
    : m_mods(acMods)
    , m_recordCollection(apRecordCollection)
    , m_policy(acPolicy)
{
}

ActorPopulationIdentity ActorPopulationIdentityResolver::Resolve(
    const GameId& acReferenceId, const GameId& acClientFormId, const GameId& acLeveledNpcPickId) const noexcept
{
    ActorPopulationIdentity identity;

    if (acReferenceId == GameId(0, 0x14))
    {
        identity.IsPlayer = true;
        identity.Classification.Class = ActorPopulationClass::kPlayer;
        identity.Source = ActorPopulationIdentitySource::kPlayer;
        return identity;
    }

    const bool isTemporary = acReferenceId.ModId == std::numeric_limits<uint32_t>::max();

    if (!isTemporary)
    {
        uint32_t resolvedReferenceFormId = 0;
        if (m_mods.ResolveServerFormId(acReferenceId, resolvedReferenceFormId))
        {
            identity.ResolvedReferenceFormId = resolvedReferenceFormId;

            if (m_recordCollection != nullptr)
            {
                const auto* const pActorReference = m_recordCollection->FindActorReferenceById(resolvedReferenceFormId);
                if (pActorReference != nullptr)
                {
                    identity.Source = ActorPopulationIdentitySource::kServerPlacedReference;
                    identity.ResolvedNpcFormId = pActorReference->m_baseObject.m_baseId;
                    identity.Classification = m_policy.ClassifyNpcBase(identity.ResolvedNpcFormId);

                    uint32_t claimedNpcFormId = 0;
                    ActorPopulationClassification claimedClassification;
                    if (ResolveNpcClaim(acClientFormId, claimedNpcFormId, claimedClassification))
                        SetClientClaim(identity, claimedNpcFormId, claimedClassification);

                    return identity;
                }
            }
        }
    }

    uint32_t claimedNpcFormId = 0;
    ActorPopulationClassification claimedClassification;

    if (isTemporary && ResolveNpcClaim(acClientFormId, claimedNpcFormId, claimedClassification))
    {
        SetClientClaim(identity, claimedNpcFormId, claimedClassification);
        identity.Source = ActorPopulationIdentitySource::kClientClaimedTemporaryBase;
        return identity;
    }

    if (ResolveNpcClaim(acLeveledNpcPickId, claimedNpcFormId, claimedClassification))
    {
        SetClientClaim(identity, claimedNpcFormId, claimedClassification);
        identity.Source = ActorPopulationIdentitySource::kClientClaimedLeveledPick;
        return identity;
    }

    // A normal reference's FormId is not an authority source. Retain a
    // resolvable claim for diagnostics, but leave the authoritative result
    // Unknown when the server cannot resolve the placed ACHR.
    if (!isTemporary && ResolveNpcClaim(acClientFormId, claimedNpcFormId, claimedClassification))
        SetClientClaim(identity, claimedNpcFormId, claimedClassification);

    return identity;
}

bool ActorPopulationIdentityResolver::ResolveNpcClaim(
    const GameId& acClaimedId, uint32_t& aResolvedNpcFormId, ActorPopulationClassification& aClassification) const noexcept
{
    if (acClaimedId == GameId{} || m_recordCollection == nullptr)
        return false;

    if (!m_mods.ResolveServerFormId(acClaimedId, aResolvedNpcFormId))
        return false;

    if (m_recordCollection->FindNpcById(aResolvedNpcFormId) == nullptr)
        return false;

    aClassification = m_policy.ClassifyNpcBase(aResolvedNpcFormId);
    return true;
}

void ActorPopulationIdentityResolver::SetClientClaim(
    ActorPopulationIdentity& aIdentity, const uint32_t aResolvedNpcFormId, const ActorPopulationClassification& acClassification) noexcept
{
    aIdentity.HasClientClaimedIdentity = true;
    aIdentity.ClientClaimedNpcFormId = aResolvedNpcFormId;
    aIdentity.ClientClaimedClassification = acClassification;
}
