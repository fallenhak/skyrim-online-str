#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Services/ActorPopulationIdentityResolver.h>

#include <cstdint>

/**
 * @brief Server-resolved population identity for one canonical actor incarnation.
 *
 * This component intentionally stores only the trusted projection of an
 * ActorPopulationIdentity. Client actor-base and leveled-pick claims are
 * diagnostics used during assignment and must never become canonical state.
 */
struct ActorPopulationIdentityComponent final
{
    ActorPopulationIdentityComponent() = default;

    explicit ActorPopulationIdentityComponent(const ActorPopulationIdentity& acIdentity) noexcept
    {
        SetTrustedIdentity(acIdentity);
    }

    void SetTrustedIdentity(const ActorPopulationIdentity& acIdentity) noexcept
    {
        Source = ActorPopulationIdentitySource::kUnknown;
        Classification = ActorPopulationClass::kUnknown;
        ResolvedReferenceFormId = 0;
        ResolvedNpcFormId = 0;
        ResolvedRaceFormId = 0;

        if (!acIdentity.IsTrusted())
            return;

        Source = acIdentity.Source;
        Classification = acIdentity.Classification.Class;
        ResolvedReferenceFormId = acIdentity.ResolvedReferenceFormId;
        ResolvedNpcFormId = acIdentity.ResolvedNpcFormId;
        ResolvedRaceFormId = acIdentity.Classification.RaceFormId;
    }

    [[nodiscard]] bool IsTrusted() const noexcept
    {
        return Source == ActorPopulationIdentitySource::kPlayer || Source == ActorPopulationIdentitySource::kServerPlacedReference ||
               Source == ActorPopulationIdentitySource::kServerNpcBase;
    }

    [[nodiscard]] bool IsTrustedCreature() const noexcept
    {
        return IsTrusted() && Classification == ActorPopulationClass::kCreature;
    }

    [[nodiscard]] bool IsTrustedPlayer() const noexcept
    {
        return IsTrusted() && Classification == ActorPopulationClass::kPlayer;
    }

    ActorPopulationIdentitySource Source = ActorPopulationIdentitySource::kUnknown;
    ActorPopulationClass Classification = ActorPopulationClass::kUnknown;
    uint32_t ResolvedReferenceFormId{};
    uint32_t ResolvedNpcFormId{};
    uint32_t ResolvedRaceFormId{};
};
