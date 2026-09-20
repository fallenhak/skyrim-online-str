#include <TiltedOnlinePCH.h>

#include <Services/AuthorityService.h>
#include <Services/PartyService.h>
#include <World.h>

#include <Events/AuthorityChangedEvent.h>
#include <Events/PartyStateChangedEvent.h>

AuthorityService::AuthorityService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_partyStateChangedConnection = aDispatcher.sink<PartyStateChangedEvent>().connect<&AuthorityService::OnPartyStateChanged>(this);
}

bool AuthorityService::HasLocalActorAuthority() const noexcept
{
    // Actor ownership is server-coordinated. Clients do not proactively steal
    // already-owned actors based on party leadership.
    return false;
}

bool AuthorityService::HasLocalWorldAuthority() const noexcept
{
    return m_world.GetPartyService().IsLeader();
}

bool AuthorityService::HasWorldAuthorityGroup() const noexcept
{
    return m_world.GetPartyService().IsInParty();
}

uint32_t AuthorityService::GetWorldAuthorityPlayerId() const noexcept
{
    return m_world.GetPartyService().GetLeaderPlayerId();
}

void AuthorityService::OnPartyStateChanged(const PartyStateChangedEvent&) noexcept
{
    PublishAuthorityChanged();
}

void AuthorityService::PublishAuthorityChanged() noexcept
{
    const bool hasGroup = HasWorldAuthorityGroup();

    AuthorityChangedEvent event{};
    event.HasLocalActorAuthority = HasLocalActorAuthority();
    event.HasLocalWorldAuthority = HasLocalWorldAuthority();
    event.HasWorldAuthorityGroup = hasGroup;
    event.WorldAuthorityPlayerId = hasGroup ? GetWorldAuthorityPlayerId() : 0;

    m_dispatcher.trigger(event);
}
