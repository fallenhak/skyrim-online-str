#include <Services/PresenceService.h>

#include <GameServer.h>
#include <World.h>

#include <Events/PlayerJoinEvent.h>
#include <Events/PlayerLeaveEvent.h>

#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerList.h>

PresenceService::PresenceService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_playerJoinConnection(aDispatcher.sink<PlayerJoinEvent>().connect<&PresenceService::OnPlayerJoin>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&PresenceService::OnPlayerLeave>(this))
{
}

void PresenceService::OnPlayerJoin(const PlayerJoinEvent& acEvent) const noexcept
{
    BroadcastPlayerList();

    NotifyPlayerJoined notify{};
    notify.PlayerId = acEvent.pPlayer->GetId();
    notify.Username = acEvent.pPlayer->GetUsername();
    notify.WorldSpaceId = acEvent.WorldSpaceId;
    notify.CellId = acEvent.CellId;
    notify.Level = acEvent.pPlayer->GetLevel();

    spdlog::debug("[Presence] Player joined {:x} {}", notify.PlayerId, notify.Username.c_str());
    GameServer::Get()->SendToPlayers(notify, acEvent.pPlayer);
}

void PresenceService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) const noexcept
{
    NotifyPlayerLeft notify{};
    notify.PlayerId = acEvent.pPlayer->GetId();
    notify.Username = acEvent.pPlayer->GetUsername();

    GameServer::Get()->SendToPlayers(notify, acEvent.pPlayer);
    BroadcastPlayerList(acEvent.pPlayer);
}

void PresenceService::BroadcastPlayerList(Player* apIgnoredPlayer) const noexcept
{
    for (Player* pSelf : m_world.GetPlayerManager())
    {
        if (pSelf == apIgnoredPlayer)
            continue;

        NotifyPlayerList playerList;
        for (Player* pPlayer : m_world.GetPlayerManager())
        {
            if (pPlayer == pSelf || pPlayer == apIgnoredPlayer)
                continue;

            playerList.Players[pPlayer->GetId()] = pPlayer->GetUsername();
        }

        pSelf->Send(playerList);
    }
}
