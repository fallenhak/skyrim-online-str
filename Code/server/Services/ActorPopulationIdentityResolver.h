#pragma once

#include "ActorPopulationPolicy.h"

#include <Structs/GameId.h>

#include <cstdint>

namespace ESLoader
{
struct RecordCollection;
}

struct ModsComponent;

enum class ActorPopulationIdentitySource : uint8_t
{
    kUnknown,
    kPlayer,
    kServerPlacedReference,
    kServerNpcBase,
    kClientClaimedTemporaryBase,
    kClientClaimedLeveledPick,
};

[[nodiscard]] const char* GetActorPopulationIdentitySourceName(ActorPopulationIdentitySource aSource) noexcept;

struct ActorPopulationIdentity
{
    bool IsPlayer{};
    uint32_t ResolvedReferenceFormId{};
    uint32_t ResolvedNpcFormId{};
    ActorPopulationClassification Classification{};

    // These fields are diagnostic only. They never replace Classification when
    // a server-resolvable placed reference exists, and never make a client claim
    // trusted for future enforcement.
    bool HasClientClaimedIdentity{};
    uint32_t ClientClaimedNpcFormId{};
    ActorPopulationClassification ClientClaimedClassification{};

    ActorPopulationIdentitySource Source = ActorPopulationIdentitySource::kUnknown;

    [[nodiscard]] bool IsTrusted() const noexcept
    {
        return Source == ActorPopulationIdentitySource::kPlayer || Source == ActorPopulationIdentitySource::kServerPlacedReference || Source == ActorPopulationIdentitySource::kServerNpcBase;
    }
};

class ActorPopulationIdentityResolver final
{
public:
    ActorPopulationIdentityResolver(const ModsComponent& acMods, const ESLoader::RecordCollection* apRecordCollection, const ActorPopulationPolicy& acPolicy) noexcept;

    [[nodiscard]] ActorPopulationIdentity Resolve(
        const GameId& acReferenceId, const GameId& acClientFormId = {}, const GameId& acLeveledNpcPickId = {}) const noexcept;

private:
    [[nodiscard]] bool ResolveNpcClaim(const GameId& acClaimedId, uint32_t& aResolvedNpcFormId, ActorPopulationClassification& aClassification) const noexcept;
    static void SetClientClaim(
        ActorPopulationIdentity& aIdentity, uint32_t aResolvedNpcFormId, const ActorPopulationClassification& acClassification) noexcept;

    const ModsComponent& m_mods;
    const ESLoader::RecordCollection* m_recordCollection;
    const ActorPopulationPolicy& m_policy;
};
