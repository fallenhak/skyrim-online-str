#include <TiltedOnlinePCH.h>

#include <Services/AuthorityService.h>
#include <Services/PartyService.h>
#include <World.h>

#include <Events/AuthorityChangedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/DisconnectedEvent.h>

#include <Messages/NotifyPartyInfo.h>

AuthorityService::AuthorityService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&AuthorityService::OnPartyJoined>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&AuthorityService::OnPartyLeft>(this);
    m_partyInfoConnection = aDispatcher.sink<NotifyPartyInfo>().connect<&AuthorityService::OnPartyInfo>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&AuthorityService::OnDisconnected>(this);
}

bool AuthorityService::HasLocalActorAuthority() const noexcept
{
    return m_world.GetPartyService().IsLeader();
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

void AuthorityService::OnPartyJoined(const PartyJoinedEvent&) noexcept
{
    PublishAuthorityChanged();
}

void AuthorityService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    PublishAuthorityChanged();
}

void AuthorityService::OnPartyInfo(const NotifyPartyInfo&) noexcept
{
    PublishAuthorityChanged();
}

void AuthorityService::OnDisconnected(const DisconnectedEvent&) noexcept
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
