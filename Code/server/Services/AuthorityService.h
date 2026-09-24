#pragma once

#include <Structs/GameId.h>

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
    bool TrySetWeatherState(Player* apPlayer, const GameId& acWeather) noexcept;
    [[nodiscard]] bool TryGetWeatherState(Player* apPlayer, GameId& aWeather) const noexcept;

private:
    World& m_world;
    GameId m_weatherState{};
};
