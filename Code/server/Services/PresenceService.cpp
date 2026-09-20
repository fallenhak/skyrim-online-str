#include <Services/PresenceService.h>

#include <World.h>
#include <Game/Player.h>

#include <Events/PlayerEnterWorldEvent.h>
#include <Events/PlayerLeaveEvent.h>

#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerList.h>

PresenceService::PresenceService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_playerEnterWorldConnection(aDispatcher.sink<PlayerEnterWorldEvent>().connect<&PresenceService::OnPlayerEnterWorld>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&PresenceService::OnPlayerLeave>(this))
{
}

void PresenceService::OnPlayerEnterWorld(const PlayerEnterWorldEvent& acEvent) noexcept
{
    const Player* const pPlayer = acEvent.pPlayer;
    if (!pPlayer || !m_inWorldPlayers.Enter(pPlayer->GetId()))
    {
        if (pPlayer)
            spdlog::debug("[Presence] Ignoring duplicate or invalid world entry for {:x}", pPlayer->GetId());
        return;
    }

    // Refresh the canonical ID/name snapshot for every visible client. The set excludes
    // authenticated and pre-world PlayerManager entries by construction.
    BroadcastPlayerList();

    // The list intentionally carries only IDs/names. Send full persistent metadata to the
    // entering player as individual join messages so overlays and player metadata initialize.
    for (const auto otherPlayerId : m_inWorldPlayers.GetOtherPlayerIds(pPlayer->GetId()))
    {
        if (const auto* pOtherPlayer = m_world.GetPlayerManager().GetById(otherPlayerId))
        {
            SendPlayerJoined(*pOtherPlayer, *pPlayer);
            SendPlayerJoined(*pPlayer, *pOtherPlayer);
        }
    }

    spdlog::debug("[Presence] Player entered world {:x} {}", pPlayer->GetId(), pPlayer->GetUsername().c_str());
}

void PresenceService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    const Player* const pPlayer = acEvent.pPlayer;
    if (!pPlayer || !m_inWorldPlayers.Leave(pPlayer->GetId()))
        return;

    for (const auto otherPlayerId : m_inWorldPlayers.GetPlayerIds())
    {
        if (const auto* pOtherPlayer = m_world.GetPlayerManager().GetById(otherPlayerId))
            SendPlayerLeft(*pPlayer, *pOtherPlayer);
    }

    BroadcastPlayerList();
    spdlog::debug("[Presence] Player left world {:x} {}", pPlayer->GetId(), pPlayer->GetUsername().c_str());
}

void PresenceService::SendPlayerJoined(const Player& acPlayer, const Player& acRecipient) const noexcept
{
    NotifyPlayerJoined notify{};
    notify.PlayerId = acPlayer.GetId();
    notify.Username = acPlayer.GetUsername();
    notify.WorldSpaceId = acPlayer.GetCellComponent().WorldSpaceId;
    notify.CellId = acPlayer.GetCellComponent().Cell;
    notify.Level = acPlayer.GetLevel();
    acRecipient.Send(notify);
}

void PresenceService::SendPlayerLeft(const Player& acPlayer, const Player& acRecipient) const noexcept
{
    NotifyPlayerLeft notify{};
    notify.PlayerId = acPlayer.GetId();
    notify.Username = acPlayer.GetUsername();
    acRecipient.Send(notify);
}

void PresenceService::BroadcastPlayerList() const noexcept
{
    for (const auto selfPlayerId : m_inWorldPlayers.GetPlayerIds())
    {
        Player* const pSelf = m_world.GetPlayerManager().GetById(selfPlayerId);
        if (!pSelf)
            continue;

        NotifyPlayerList playerList;
        for (const auto playerId : m_inWorldPlayers.GetPlayerIds())
        {
            if (playerId == pSelf->GetId())
                continue;

            if (const auto* pPlayer = m_world.GetPlayerManager().GetById(playerId))
                playerList.Players[playerId] = pPlayer->GetUsername();
        }

        pSelf->Send(playerList);
    }
}
