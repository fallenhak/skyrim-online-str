#pragma once

struct World;

/**
 * @brief Client-side view of local world authority.
 *
 * This initially mirrors STR's existing party-leader rule. Keeping that rule
 * behind a dedicated service lets actor ownership policy change later without
 * coupling CharacterService to social-party state.
 */
struct AuthorityService
{
    explicit AuthorityService(World& aWorld) noexcept;
    ~AuthorityService() noexcept = default;

    TP_NOCOPYMOVE(AuthorityService);

    [[nodiscard]] bool HasLocalActorAuthority() const noexcept;

private:
    World& m_world;
};
