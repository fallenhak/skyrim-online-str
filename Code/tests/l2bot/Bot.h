#pragma once

#include <chrono>

#include <Client.hpp>
#include <Messages/Message.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// A scripted client with no game: it walks the same session protocol the game client does
// (auth, character list, create or select, snapshot, ready, player assignment, cell entry)
// and records what the server told it (#91).
// Vitals the bot's character reports at assignment (health, magicka, stamina).
inline constexpr float kBotMaxVitals[3] = {100.f, 50.f, 80.f};

class Bot final : public TiltedPhoques::Client
{
public:
    enum class Phase
    {
        kConnecting,
        kAuthenticating,
        kSelectingCharacter,
        kApplyingSnapshot,
        kAwaitingReady,
        kAwaitingAssignment,
        kInWorld,
        kFailed,
    };

    struct Config
    {
        std::string Name;
        std::uint64_t DiscordId{};
        std::string HmacSecret;
        std::uint32_t CellBaseId{};
    };

    explicit Bot(Config aConfig);

    bool Send(const ClientMessage& acMessage) const noexcept;
    // Closes the connection without counting the disconnect as a failure.
    void Shutdown() noexcept;

    void OnConsume(const void* apData, uint32_t aSize) override;
    void OnConnected() override;
    void OnDisconnected(EDisconnectReason aReason) override;
    void OnUpdate() override {}

    [[nodiscard]] Phase GetPhase() const noexcept { return m_phase; }
    [[nodiscard]] const std::string& GetName() const noexcept { return m_config.Name; }
    [[nodiscard]] const std::string& GetFailure() const noexcept { return m_failure; }
    [[nodiscard]] std::uint32_t GetServerId() const noexcept { return m_serverId; }
    [[nodiscard]] std::uint64_t GetCharacterId() const noexcept { return m_characterId; }
    // The fixture plugin's mod id in this session; GameIds of fixture forms use it.
    [[nodiscard]] std::uint32_t GetFixtureModId() const noexcept { return m_fixtureModId; }
    [[nodiscard]] std::uint32_t GetOwnershipEpoch() const noexcept { return m_ownershipEpoch; }

    // Called for every server message after the bot's own session handling.
    std::function<void(const ServerMessage&)> OnMessage;

private:
    void Fail(std::string aReason) noexcept;
    // The server takes a character's cell from its movement updates, not from the cell entry.
    void SendMovement() noexcept;
    void HandleMessage(const ServerMessage& acMessage) noexcept;

    Config m_config;
    Phase m_phase{Phase::kConnecting};
    std::string m_failure;
    std::uint32_t m_fixtureModId{};
    std::uint64_t m_characterId{};
    std::uint32_t m_serverId{};
    std::uint32_t m_ownershipEpoch{};
    std::uint64_t m_movementTick{};
    std::uint32_t m_assignCookie{1};
    bool m_shuttingDown{};
};

[[nodiscard]] const char* ToString(Bot::Phase aPhase) noexcept;

// An HS256 session token in the auth service's format, signed with the server's secret.
[[nodiscard]] std::string MakeSessionToken(std::uint64_t aDiscordId, const std::string& acName, const std::string& acHmacSecret);
