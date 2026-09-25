#pragma once

struct World;
struct Player;

/**
 * @brief Centralizes decisions about which client may drive replicated world state.
 *
 * Actor ownership is server-coordinated and world authority is elected from
 * connected players independently of social parties.
 */
struct AuthorityService
{
    explicit AuthorityService(World& aWorld) noexcept;
    ~AuthorityService() noexcept = default;

    TP_NOCOPYMOVE(AuthorityService);

    [[nodiscard]] bool CanClaimActor(Player* apClaimant, Player* apCurrentOwner) const noexcept;
    [[nodiscard]] bool IsWorldAuthority(const Player* apPlayer) const noexcept;

private:
    World& m_world;
};
