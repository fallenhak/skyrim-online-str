#pragma once

struct Player;

/**
 * @brief Dispatched after a persistent player entity is created and the session enters InWorld.
 */
struct PlayerEnterWorldEvent
{
    PlayerEnterWorldEvent(const Player* apPlayer)
        : pPlayer{apPlayer}
    {
    }

    const Player* pPlayer;
};
