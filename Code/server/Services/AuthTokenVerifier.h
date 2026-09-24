#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Auth
{
struct SessionClaims
{
    std::uint64_t DiscordId{};
    std::string DisplayName;
    std::string AvatarUrl;
    std::int64_t ExpiresAt{};
};

/** Verify the auth-service HS256 token and extract signed Discord claims. */
[[nodiscard]] bool VerifySessionToken(std::string_view acToken, std::string_view acHmacSecret,
    SessionClaims& aClaims, std::string& aErrorKey) noexcept;
} // namespace Auth
