#pragma once

#include <Structs/GameId.h>

/**
 * @brief Server-owned canonical weather state for the shared world.
 */
class WeatherState
{
public:
    bool SetCurrent(const GameId& acWeather) noexcept
    {
        if (!acWeather || acWeather == m_currentWeather)
            return false;

        m_currentWeather = acWeather;
        return true;
    }

    [[nodiscard]] bool TryGetCurrent(GameId& aWeather) const noexcept
    {
        if (!m_currentWeather)
            return false;

        aWeather = m_currentWeather;
        return true;
    }

private:
    GameId m_currentWeather{};
};
