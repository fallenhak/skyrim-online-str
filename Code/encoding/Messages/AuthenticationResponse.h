#pragma once

#include "Message.h"
#include <Structs/Mods.h>
#include <Structs/ServerSettings.h>

struct AuthenticationResponse final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kAuthenticationResponse;

    enum class ResponseType : uint8_t
    {
        kAccepted,
        kWrongVersion,
        kModsMismatch,
        kClientModsDisallowed,
        kWrongPassword,
        kServerFull,
        kInvalidAuthToken
    };

    AuthenticationResponse()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AuthenticationResponse& achRhs) const noexcept { return GetOpcode() == achRhs.GetOpcode() && Type == achRhs.Type && UserMods == achRhs.UserMods && Settings == achRhs.Settings && PlayerId == achRhs.PlayerId && AuthErrorKey == achRhs.AuthErrorKey && DisplayName == achRhs.DisplayName && AvatarUrl == achRhs.AvatarUrl; }

    ResponseType Type;
    bool SKSEActive{false};
    bool MO2Active{false};
    String Version;
    Mods UserMods{};
    ServerSettings Settings{};
    uint32_t PlayerId{};
    String AuthErrorKey{};
    String DisplayName{};
    String AvatarUrl{};
};
