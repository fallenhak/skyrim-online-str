#pragma once

#include <TiltedCore/Stl.hpp>

#include <cstdint>

/**
 * @brief Tracks the server's explicit set of persistent-world-visible players.
 */
struct InWorldPresenceState final
{
    [[nodiscard]] bool Enter(const std::uint32_t aPlayerId) noexcept
    {
        if (aPlayerId == 0 || m_playerIds.contains(aPlayerId))
            return false;

        m_playerIds.insert(aPlayerId);
        return true;
    }

    [[nodiscard]] bool Leave(const std::uint32_t aPlayerId) noexcept
    {
        if (!m_playerIds.contains(aPlayerId))
            return false;

        m_playerIds.erase(aPlayerId);
        return true;
    }

    [[nodiscard]] bool Contains(const std::uint32_t aPlayerId) const noexcept
    {
        return m_playerIds.contains(aPlayerId);
    }

    [[nodiscard]] const TiltedPhoques::Set<std::uint32_t>& GetPlayerIds() const noexcept
    {
        return m_playerIds;
    }

    [[nodiscard]] TiltedPhoques::Vector<std::uint32_t> GetOtherPlayerIds(const std::uint32_t aPlayerId) const
    {
        TiltedPhoques::Vector<std::uint32_t> playerIds;
        for (const auto playerId : m_playerIds)
        {
            if (playerId != aPlayerId)
                playerIds.push_back(playerId);
        }

        return playerIds;
    }

private:
    TiltedPhoques::Set<std::uint32_t> m_playerIds;
};

/**
 * @brief Tracks client connection state separately from local persistent-world presence.
 */
struct ClientPresenceState final
{
    void Connect(const std::uint32_t aLocalPlayerId) noexcept
    {
        m_connected = true;
        m_inWorld = false;
        m_localPlayerId = aLocalPlayerId;
        m_remotePlayerIds.clear();
    }

    void Disconnect() noexcept
    {
        m_connected = false;
        m_inWorld = false;
        m_localPlayerId = 0;
        m_remotePlayerIds.clear();
    }

    void SetInWorld(const bool aInWorld) noexcept
    {
        m_inWorld = aInWorld && m_connected && m_localPlayerId != 0;
    }

    [[nodiscard]] bool IsConnected() const noexcept { return m_connected; }
    [[nodiscard]] bool IsInWorld() const noexcept { return m_inWorld; }
    [[nodiscard]] std::uint32_t GetLocalPlayerId() const noexcept { return m_localPlayerId; }

    [[nodiscard]] std::uint32_t GetWorldAuthorityPlayerId() const noexcept
    {
        if (!m_connected || !m_inWorld || m_localPlayerId == 0)
            return 0;

        std::uint32_t authorityId = m_localPlayerId;
        for (const auto playerId : m_remotePlayerIds)
        {
            if (playerId < authorityId)
                authorityId = playerId;
        }

        return authorityId;
    }

    void ClearRemotePlayers() noexcept { m_remotePlayerIds.clear(); }

    void AddRemotePlayer(const std::uint32_t aPlayerId) noexcept
    {
        if (aPlayerId == 0 || aPlayerId == m_localPlayerId)
            return;

        m_remotePlayerIds.insert(aPlayerId);
    }

    void RemoveRemotePlayer(const std::uint32_t aPlayerId) noexcept
    {
        m_remotePlayerIds.erase(aPlayerId);
    }

    [[nodiscard]] const TiltedPhoques::Set<std::uint32_t>& GetRemotePlayerIds() const noexcept
    {
        return m_remotePlayerIds;
    }

private:
    bool m_connected{false};
    bool m_inWorld{false};
    std::uint32_t m_localPlayerId{0};
    TiltedPhoques::Set<std::uint32_t> m_remotePlayerIds;
};
