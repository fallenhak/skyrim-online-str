#include <TiltedOnlinePCH.h>

#include <Services/AuthorityService.h>
#include <Services/PartyService.h>
#include <World.h>

AuthorityService::AuthorityService(World& aWorld) noexcept
    : m_world(aWorld)
{
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
