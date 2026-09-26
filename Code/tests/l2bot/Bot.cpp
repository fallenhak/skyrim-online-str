#include "Bot.h"

#include <fixture/L2Fixture.h>

#include <BuildInfo.h>

#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/CharacterReadyRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/CreateCharacterRequest.h>
#include <Messages/EnterInteriorCellRequest.h>
#include <Messages/NotifyCharacterCreateResult.h>
#include <Messages/NotifyCharacterEnteredWorld.h>
#include <Messages/NotifyCharacterList.h>
#include <Messages/NotifyCharacterLoadSnapshot.h>
#include <Messages/NotifyCharacterReadyResult.h>
#include <Messages/NotifyCharacterSelectionResult.h>
#include <Messages/RequestCharacterList.h>
#include <Messages/SelectCharacterRequest.h>
#include <Messages/ServerMessageFactory.h>

#include <Packet.hpp>
#include <TiltedCore/ViewBuffer.hpp>

#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>

#include <spdlog/spdlog.h>

#include <chrono>

namespace
{
constexpr std::uint32_t kPlayerReference = 0x14;

std::string Base64Url(const std::string& acData)
{
    std::string encoded;
    CryptoPP::StringSource source(acData, true, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(encoded), false));
    for (char& character : encoded)
    {
        if (character == '+')
            character = '-';
        else if (character == '/')
            character = '_';
    }
    while (!encoded.empty() && encoded.back() == '=')
        encoded.pop_back();
    return encoded;
}
} // namespace

std::string MakeSessionToken(const std::uint64_t aDiscordId, const std::string& acName, const std::string& acHmacSecret)
{
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string claims = "{\"iss\":\"sos-auth\",\"sub\":\"discord:" + std::to_string(aDiscordId) + "\",\"name\":\"" + acName +
                               "\",\"avatar\":\"\",\"iat\":" + std::to_string(now) + ",\"exp\":" + std::to_string(now + 3600) + "}";

    const std::string signingInput = Base64Url(R"({"alg":"HS256","typ":"JWT"})") + "." + Base64Url(claims);
    CryptoPP::HMAC<CryptoPP::SHA256> hmac(reinterpret_cast<const CryptoPP::byte*>(acHmacSecret.data()), acHmacSecret.size());
    std::string signature(CryptoPP::SHA256::DIGESTSIZE, '\0');
    hmac.CalculateDigest(
        reinterpret_cast<CryptoPP::byte*>(signature.data()), reinterpret_cast<const CryptoPP::byte*>(signingInput.data()), signingInput.size());
    return signingInput + "." + Base64Url(signature);
}

const char* ToString(const Bot::Phase aPhase) noexcept
{
    switch (aPhase)
    {
    case Bot::Phase::kConnecting: return "connecting";
    case Bot::Phase::kAuthenticating: return "authenticating";
    case Bot::Phase::kSelectingCharacter: return "selecting character";
    case Bot::Phase::kApplyingSnapshot: return "applying snapshot";
    case Bot::Phase::kAwaitingReady: return "awaiting ready";
    case Bot::Phase::kAwaitingAssignment: return "awaiting assignment";
    case Bot::Phase::kInWorld: return "in world";
    case Bot::Phase::kFailed: return "failed";
    }
    return "?";
}

Bot::Bot(Config aConfig)
    : m_config(std::move(aConfig))
{
}

bool Bot::Send(const ClientMessage& acMessage) const noexcept
{
    if (!IsConnected())
        return false;

    TiltedPhoques::Buffer buffer(1 << 16);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // kPayload
    acMessage.Serialize(writer);
    TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());
    Client::Send(&packet);
    return true;
}

void Bot::Shutdown() noexcept
{
    m_shuttingDown = true;
    Close();
}

void Bot::SendMovement() noexcept
{
    Move(m_serverId, m_ownershipEpoch, 0.f);
}

void Bot::Move(const std::uint32_t aServerId, const std::uint32_t aOwnershipEpoch, const float aX) noexcept
{
    ClientReferencesMoveRequest request{};
    request.Tick = ++m_movementTick;
    auto& update = request.Updates[aServerId];
    update.OwnershipEpoch = aOwnershipEpoch;
    update.UpdatedMovement.CellId = GameId(m_fixtureModId, m_config.CellBaseId);
    update.UpdatedMovement.Position.x = aX;
    Send(request);
}

void Bot::Fail(std::string aReason) noexcept
{
    spdlog::error("[{}] failed while {}: {}", m_config.Name, ToString(m_phase), aReason);
    m_failure = std::move(aReason);
    m_phase = Phase::kFailed;
}

void Bot::OnConnected()
{
    spdlog::info("[{}] connected", m_config.Name);
    m_phase = Phase::kAuthenticating;

    AuthenticationRequest request{};
    request.Version = BUILD_COMMIT;
    request.Username = m_config.Name.c_str();
    request.AuthToken = MakeSessionToken(m_config.DiscordId, m_config.Name, m_config.HmacSecret).c_str();
    request.Level = 1;

    auto& fixture = request.UserMods.ModList.emplace_back();
    fixture.Filename = L2Fixture::kPluginName;
    fixture.Id = 0;
    fixture.IsLite = false;

    Send(request);
}

void Bot::OnDisconnected(const EDisconnectReason aReason)
{
    if (m_phase != Phase::kFailed && !m_shuttingDown)
        Fail("disconnected (reason " + std::to_string(static_cast<int>(aReason)) + ")");
}

void Bot::OnConsume(const void* apData, const uint32_t aSize)
{
    ServerMessageFactory factory;
    TiltedPhoques::ViewBuffer buffer(static_cast<uint8_t*>(const_cast<void*>(apData)), aSize);
    TiltedPhoques::Buffer::Reader reader(&buffer);

    const auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        Fail("could not parse a server packet");
        return;
    }

    HandleMessage(*pMessage);
    if (OnMessage)
        OnMessage(*pMessage);
}

void Bot::HandleMessage(const ServerMessage& acMessage) noexcept
{
    switch (acMessage.GetOpcode())
    {
    case kAuthenticationResponse:
    {
        const auto& response = static_cast<const AuthenticationResponse&>(acMessage);
        if (response.Type != AuthenticationResponse::ResponseType::kAccepted)
        {
            Fail("authentication rejected, type " + std::to_string(static_cast<int>(response.Type)) + " " + response.AuthErrorKey.c_str());
            return;
        }
        for (const auto& mod : response.UserMods.ModList)
        {
            if (mod.Filename == L2Fixture::kPluginName)
                m_fixtureModId = mod.Id;
        }
        m_phase = Phase::kSelectingCharacter;
        Send(RequestCharacterList{});
        break;
    }
    case kNotifyCharacterList:
    {
        if (m_phase != Phase::kSelectingCharacter)
            break;
        const auto& list = static_cast<const NotifyCharacterList&>(acMessage);
        if (!list.Characters.empty())
        {
            SelectCharacterRequest request{};
            request.CharacterId = list.Characters.front().CharacterId;
            Send(request);
        }
        else
        {
            CreateCharacterRequest request{};
            request.SlotIndex = 0;
            request.Name = m_config.Name.c_str();
            Send(request);
        }
        break;
    }
    case kNotifyCharacterCreateResult:
    {
        const auto& result = static_cast<const NotifyCharacterCreateResult&>(acMessage);
        if (result.Status != CharacterCreateStatus::kSuccess)
            Fail("character creation failed, status " + std::to_string(static_cast<int>(result.Status)));
        break;
    }
    case kNotifyCharacterSelectionResult:
    {
        const auto& result = static_cast<const NotifyCharacterSelectionResult&>(acMessage);
        if (result.Status != CharacterSelectionStatus::kSuccess)
            Fail("character selection failed, status " + std::to_string(static_cast<int>(result.Status)));
        break;
    }
    case kNotifyCharacterLoadSnapshot:
    {
        // The game applies the snapshot to the player here; the bot has nothing to apply.
        const auto& snapshot = static_cast<const NotifyCharacterLoadSnapshot&>(acMessage);
        m_characterId = snapshot.Snapshot.CharacterId;
        m_phase = Phase::kAwaitingReady;
        CharacterReadyRequest request{};
        request.CharacterId = m_characterId;
        Send(request);
        break;
    }
    case kNotifyCharacterReadyResult:
    {
        const auto& result = static_cast<const NotifyCharacterReadyResult&>(acMessage);
        if (result.Status != CharacterReadyStatus::kProceed)
        {
            Fail("ready refused, status " + std::to_string(static_cast<int>(result.Status)));
            return;
        }
        m_phase = Phase::kAwaitingAssignment;
        AssignCharacterRequest request{};
        request.Cookie = m_assignCookie;
        request.ReferenceId = GameId(0, kPlayerReference);
        // FormId stays empty: the server rejects a base id on the player reference.
        request.CellId = GameId(m_fixtureModId, m_config.CellBaseId);
        for (uint32_t i = 0; i < 3; ++i)
        {
            request.CurrentActorData.InitialActorValues.ActorValuesList[24 + i] = kBotMaxVitals[i];
            request.CurrentActorData.InitialActorValues.ActorMaxValuesList[24 + i] = kBotMaxVitals[i];
        }
        Send(request);
        break;
    }
    case kAssignCharacterResponse:
    {
        const auto& response = static_cast<const AssignCharacterResponse&>(acMessage);
        if (response.Cookie == m_assignCookie && m_phase == Phase::kAwaitingAssignment)
        {
            m_serverId = response.ServerId;
            m_ownershipEpoch = response.OwnershipEpoch;
        }
        break;
    }
    case kNotifyCharacterEnteredWorld:
    {
        if (m_phase != Phase::kAwaitingAssignment)
            break;
        spdlog::info("[{}] entered the world as server id {:x}", m_config.Name, m_serverId);
        m_phase = Phase::kInWorld;
        EnterInteriorCellRequest request{};
        request.CellId = GameId(m_fixtureModId, m_config.CellBaseId);
        Send(request);
        SendMovement();
        break;
    }
    default: break;
    }
}
