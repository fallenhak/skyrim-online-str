#include <Services/AuthorityService.h>

#include <Game/Player.h>
#include <World.h>

AuthorityService::AuthorityService(World& aWorld) noexcept
    : m_world(aWorld)
{
}

bool AuthorityService::CanClaimActor(Player*, Player*) const noexcept
{
    // Persistent-world ownership is not transferred merely because another
    // player has a social/party role. Initial discovery assigns an owner and
    // CharacterService hands ownership to another in-range player when the
    // current owner relinquishes control or becomes unavailable.
    return false;
}

bool AuthorityService::IsWorldAuthority(const Player* apPlayer) const noexcept
{
    if (!apPlayer)
        return false;

    const Player* pAuthority = nullptr;
    for (const Player* pPlayer : m_world.GetPlayerManager())
    {
        if (!pAuthority || pPlayer->GetId() < pAuthority->GetId())
            pAuthority = pPlayer;
    }

    return pAuthority == apPlayer;
}
