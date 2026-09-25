#include <TiltedOnlinePCH.h>

#include <Services/AuthorityService.h>
#include <Services/PresenceService.h>
#include <World.h>

#include <Events/AuthorityChangedEvent.h>
#include <Events/PresenceChangedEvent.h>

AuthorityService::AuthorityService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_presenceChangedConnection = aDispatcher.sink<PresenceChangedEvent>().connect<&AuthorityService::OnPresenceChanged>(this);
}

bool AuthorityService::HasLocalActorAuthority() const noexcept
{
    // Actor ownership is server-coordinated. Clients do not proactively steal
    // already-owned actors based on party leadership.
    return false;
}

bool AuthorityService::HasLocalWorldAuthority() const noexcept
{
    const auto& presence = m_world.GetPresenceService();
    return presence.IsConnected() && presence.GetLocalPlayerId() == presence.GetWorldAuthorityPlayerId();
}

bool AuthorityService::HasWorldAuthoritySource() const noexcept
{
    return m_world.GetPresenceService().IsConnected();
}

uint32_t AuthorityService::GetWorldAuthorityPlayerId() const noexcept
{
    return m_world.GetPresenceService().GetWorldAuthorityPlayerId();
}

void AuthorityService::OnPresenceChanged(const PresenceChangedEvent&) noexcept
{
    PublishAuthorityChanged();
}

void AuthorityService::PublishAuthorityChanged() noexcept
{
    const bool hasSource = HasWorldAuthoritySource();

    AuthorityChangedEvent event{};
    event.HasLocalActorAuthority = HasLocalActorAuthority();
    event.HasLocalWorldAuthority = HasLocalWorldAuthority();
    event.HasWorldAuthoritySource = hasSource;
    event.WorldAuthorityPlayerId = hasSource ? GetWorldAuthorityPlayerId() : 0;

    m_dispatcher.trigger(event);
}
