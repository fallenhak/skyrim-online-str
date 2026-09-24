#include <Services/AuthTokenVerifier.h>

#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>
#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace
{
constexpr std::string_view kTestSecret = "test-hmac-key-that-is-at-least-32-bytes-long";

std::string Base64Url(const std::string_view acData)
{
    std::string encoded;
    CryptoPP::StringSource source(std::string(acData), true, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(encoded), false));
    for (char& character : encoded)
    {
        if (character == '+') character = '-';
        else if (character == '/') character = '_';
    }
    while (!encoded.empty() && encoded.back() == '=') encoded.pop_back();
    return encoded;
}

std::string MakeToken(const std::string_view acClaims, const std::string_view acSecret = kTestSecret)
{
    const std::string header = Base64Url(R"({"alg":"HS256","typ":"JWT"})");
    const std::string payload = Base64Url(acClaims);
    const std::string signingInput = header + "." + payload;
    CryptoPP::HMAC<CryptoPP::SHA256> hmac(reinterpret_cast<const CryptoPP::byte*>(acSecret.data()), acSecret.size());
    std::string signature(CryptoPP::SHA256::DIGESTSIZE, '\0');
    hmac.CalculateDigest(reinterpret_cast<CryptoPP::byte*>(signature.data()), reinterpret_cast<const CryptoPP::byte*>(signingInput.data()), signingInput.size());
    return signingInput + "." + Base64Url(signature);
}

std::int64_t Now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Claims(const std::int64_t aIssuedAt, const std::int64_t aExpiresAt)
{
    return "{\"iss\":\"sos-auth\",\"sub\":\"discord:123456789012345678\",\"name\":\"Kerim \\u00d6zt\\u00fcrk \\\"STR\\\"\",\"avatar\":\"https://cdn.discordapp.com/avatar.png\",\"iat\":" +
        std::to_string(aIssuedAt) + ",\"exp\":" + std::to_string(aExpiresAt) + "}";
}
} // namespace

TEST(AuthTokenVerifierTests, AcceptsSignedDiscordIdentityAndDecodesJsonEscapes)
{
    const auto now = Now();
    const auto token = MakeToken(Claims(now, now + 12 * 60 * 60));
    Auth::SessionClaims claims{};
    std::string errorKey;

    ASSERT_TRUE(Auth::VerifySessionToken(token, kTestSecret, claims, errorKey));
    EXPECT_TRUE(errorKey.empty());
    EXPECT_EQ(claims.DiscordId, 123456789012345678ULL);
    EXPECT_EQ(claims.DisplayName, "Kerim Öztürk \"STR\"");
    EXPECT_EQ(claims.AvatarUrl, "https://cdn.discordapp.com/avatar.png");
}

TEST(AuthTokenVerifierTests, RejectsSignatureTampering)
{
    const auto now = Now();
    auto token = MakeToken(Claims(now, now + 3600));
    const auto signatureStart = token.find_last_of('.') + 1;
    token[signatureStart] = token[signatureStart] == 'A' ? 'B' : 'A';
    Auth::SessionClaims claims{};
    std::string errorKey;
    EXPECT_FALSE(Auth::VerifySessionToken(token, kTestSecret, claims, errorKey));
    EXPECT_EQ(errorKey, "auth.token_invalid");
}

TEST(AuthTokenVerifierTests, RejectsNonCanonicalBase64UrlTailBits)
{
    constexpr std::string_view kBase64UrlAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const auto now = Now();
    auto token = MakeToken(Claims(now, now + 3600));
    const auto finalCharacterIndex = kBase64UrlAlphabet.find(token.back());
    ASSERT_NE(finalCharacterIndex, std::string_view::npos);
    ASSERT_LT(finalCharacterIndex + 1, kBase64UrlAlphabet.size());
    token.back() = kBase64UrlAlphabet[finalCharacterIndex + 1];

    Auth::SessionClaims claims{};
    std::string errorKey;
    EXPECT_FALSE(Auth::VerifySessionToken(token, kTestSecret, claims, errorKey));
    EXPECT_EQ(errorKey, "auth.token_invalid");
}

TEST(AuthTokenVerifierTests, RejectsExpiredOrOverlongSessions)
{
    const auto now = Now();
    Auth::SessionClaims claims{};
    std::string errorKey;
    EXPECT_FALSE(Auth::VerifySessionToken(MakeToken(Claims(now - 7200, now - 1)), kTestSecret, claims, errorKey));
    EXPECT_EQ(errorKey, "auth.token_expired");
    EXPECT_FALSE(Auth::VerifySessionToken(MakeToken(Claims(now, now + 12 * 60 * 60 + 1)), kTestSecret, claims, errorKey));
    EXPECT_EQ(errorKey, "auth.token_expired");
}

TEST(AuthTokenVerifierTests, RejectsForeignSubjectAndDuplicateClaims)
{
    const auto now = Now();
    Auth::SessionClaims claims{};
    std::string errorKey;
    const auto foreign = MakeToken("{\"iss\":\"sos-auth\",\"sub\":\"discord:42x\",\"name\":\"Name\",\"avatar\":\"\",\"iat\":" + std::to_string(now) + ",\"exp\":" + std::to_string(now + 3600) + "}");
    EXPECT_FALSE(Auth::VerifySessionToken(foreign, kTestSecret, claims, errorKey));
    const auto duplicate = MakeToken("{\"iss\":\"sos-auth\",\"sub\":\"discord:42\",\"sub\":\"discord:43\",\"name\":\"Name\",\"avatar\":\"\",\"iat\":" + std::to_string(now) + ",\"exp\":" + std::to_string(now + 3600) + "}");
    EXPECT_FALSE(Auth::VerifySessionToken(duplicate, kTestSecret, claims, errorKey));
}

TEST(AuthTokenVerifierTests, MissingServerSecretFailsClosed)
{
    const auto now = Now();
    Auth::SessionClaims claims{};
    std::string errorKey;
    EXPECT_FALSE(Auth::VerifySessionToken(MakeToken(Claims(now, now + 3600)), "short", claims, errorKey));
    EXPECT_EQ(errorKey, "auth.server_misconfigured");
}
