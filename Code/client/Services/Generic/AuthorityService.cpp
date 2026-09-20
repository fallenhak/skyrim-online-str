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
