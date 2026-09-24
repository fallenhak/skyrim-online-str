#pragma once

#include "Message.h"
#include <Structs/Mods.h>
#include <TiltedCore/Buffer.hpp>
#include <Structs/GameId.h>
#include <Structs/TimeModel.h>
#include <Structs/Vector3_NetQuantize.h>

struct AuthenticationRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kAuthenticationRequest;

    AuthenticationRequest()
        : ClientMessage(Opcode)
    {
    }

    virtual ~AuthenticationRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AuthenticationRequest& achRhs) const noexcept
    {
        return GetOpcode() == achRhs.GetOpcode() && DiscordId == achRhs.DiscordId && SKSEActive == achRhs.SKSEActive && MO2Active == achRhs.MO2Active && Token == achRhs.Token &&
               Version == achRhs.Version && UserMods == achRhs.UserMods && Username == achRhs.Username && RaceFormId == achRhs.RaceFormId && Sex == achRhs.Sex && Position == achRhs.Position &&
               WorldSpaceFormId == achRhs.WorldSpaceFormId && CellFormId == achRhs.CellFormId && WorldSpaceId == achRhs.WorldSpaceId && CellId == achRhs.CellId && Level == achRhs.Level &&
               PlayerTime == achRhs.PlayerTime && AuthToken == achRhs.AuthToken;
    }

    uint64_t DiscordId{};
    bool SKSEActive{};
    bool MO2Active{};
    String Token{};
    String Version{};
    Mods UserMods{};
    String Username{};
    uint32_t RaceFormId{};
    uint8_t Sex{};
    Vector3_NetQuantize Position{};
    uint32_t WorldSpaceFormId{};
    uint32_t CellFormId{};
    GameId WorldSpaceId{};
    GameId CellId{};
    uint16_t Level{};
    TimeModel PlayerTime{};
    // Separate from Token, which remains the legacy server password field.
    String AuthToken{};
};
