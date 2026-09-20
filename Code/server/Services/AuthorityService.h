#pragma once

struct World;
struct Player;

/**
 * @brief Centralizes decisions about which client may drive replicated world state.
 *
 * The first implementation deliberately preserves STR's existing party-leader
 * semantics. Callers should depend on this service instead of PartyService so
 * authority selection can evolve independently from social grouping.
 */
struct AuthorityService
{
    explicit AuthorityService(World& aWorld) noexcept;
    ~AuthorityService() noexcept = default;

    TP_NOCOPYMOVE(AuthorityService);

    [[nodiscard]] bool CanClaimActor(Player* apClaimant, Player* apCurrentOwner) const noexcept;

private:
    World& m_world;
};
