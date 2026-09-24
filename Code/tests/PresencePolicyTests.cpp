#include <TiltedCore/Stl.hpp>

#include <catch2/catch.hpp>

#include <Structs/PresencePolicy.h>

#include <algorithm>

TEST_CASE("Server presence is limited to persistent-world players", "[presence.server]")
{
    InWorldPresenceState presence;

    REQUIRE_FALSE(presence.Contains(10));
    REQUIRE_FALSE(presence.Leave(10));
    REQUIRE(presence.Enter(10));
    REQUIRE(presence.Contains(10));
    REQUIRE_FALSE(presence.Enter(10));

    REQUIRE(presence.Enter(20));
    const auto existingPlayers = presence.GetOtherPlayerIds(20);
    REQUIRE(std::find(existingPlayers.begin(), existingPlayers.end(), 10) != existingPlayers.end());
    REQUIRE(std::find(existingPlayers.begin(), existingPlayers.end(), 20) == existingPlayers.end());

    REQUIRE(presence.Leave(10));
    REQUIRE_FALSE(presence.Contains(10));
    REQUIRE_FALSE(presence.Leave(10));

    const auto visiblePlayers = presence.GetPlayerIds();
    REQUIRE(visiblePlayers.size() == 1);
    REQUIRE(visiblePlayers.contains(20));
}

TEST_CASE("Client presence keeps authority inactive until local world entry", "[presence.client]")
{
    ClientPresenceState presence;
    presence.Connect(50);

    REQUIRE(presence.IsConnected());
    REQUIRE_FALSE(presence.IsInWorld());
    REQUIRE(presence.GetWorldAuthorityPlayerId() == 0);

    // Remote presence may arrive before local world sync and is cached without making the
    // authenticated-but-pre-world local connection an authority source.
    presence.AddRemotePlayer(5);
    REQUIRE(presence.GetWorldAuthorityPlayerId() == 0);

    presence.SetInWorld(true);
    REQUIRE(presence.IsInWorld());
    REQUIRE(presence.GetWorldAuthorityPlayerId() == 5);

    presence.AddRemotePlayer(5);
    REQUIRE(presence.GetRemotePlayerIds().size() == 1);
    presence.RemoveRemotePlayer(5);
    REQUIRE(presence.GetWorldAuthorityPlayerId() == 50);

    presence.Disconnect();
    REQUIRE_FALSE(presence.IsConnected());
    REQUIRE_FALSE(presence.IsInWorld());
    REQUIRE(presence.GetWorldAuthorityPlayerId() == 0);
    REQUIRE(presence.GetRemotePlayerIds().empty());
}
