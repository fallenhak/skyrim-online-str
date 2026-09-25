#include <Services/WeatherState.h>

#include <catch2/catch.hpp>

TEST_CASE("Server weather state is independent of the elected reporter", "[weather.server]")
{
    WeatherState state;
    GameId current{};

    REQUIRE_FALSE(state.TryGetCurrent(current));

    const GameId firstWeather{1, 0x80};
    REQUIRE(state.SetCurrent(firstWeather));
    REQUIRE_FALSE(state.SetCurrent(firstWeather));

    // Changing which player reports natural transitions does not reset the
    // canonical value stored by the server.
    REQUIRE(state.TryGetCurrent(current));
    REQUIRE(current == firstWeather);

    const GameId nextWeather{2, 0x100};
    REQUIRE(state.SetCurrent(nextWeather));
    REQUIRE(state.TryGetCurrent(current));
    REQUIRE(current == nextWeather);
}

TEST_CASE("Server weather state rejects an empty weather ID", "[weather.server]")
{
    WeatherState state;
    GameId current{};

    REQUIRE_FALSE(state.SetCurrent(GameId{}));
    REQUIRE_FALSE(state.TryGetCurrent(current));
}
