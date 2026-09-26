#include <Services/AuthTokenVerifier.h>

#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>

#include <array>
#include <charconv>
#include <chrono>
#include <limits>
#include <map>
#include <string>

namespace
{
// Must match TOKEN_TTL_SECONDS in Tools/AuthService/auth_service.py.
constexpr std::int64_t kMaxTokenLifetimeSeconds = 7 * 24 * 60 * 60;

bool DecodeBase64Url(const std::string_view acEncoded, std::string& aDecoded)
{
    aDecoded.clear();
    if (acEncoded.empty() || acEncoded.size() > 8192 || acEncoded.size() % 4 == 1)
        return false;

    std::string base64;
    base64.reserve(acEncoded.size() + 3);
    for (char character : acEncoded)
    {
        if (character == '-')
            base64.push_back('+');
        else if (character == '_')
            base64.push_back('/');
        else if ((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9'))
            base64.push_back(character);
        else
            return false;
    }
    while (base64.size() % 4 != 0)
        base64.push_back('=');

    try
    {
        CryptoPP::StringSource source(base64, true, new CryptoPP::Base64Decoder(new CryptoPP::StringSink(aDecoded)));
    }
    catch (const CryptoPP::Exception&)
    {
        aDecoded.clear();
        return false;
    }

    std::string canonical;
    CryptoPP::StringSource source(aDecoded, true, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(canonical), false));
    for (char& character : canonical)
    {
        if (character == '+')
            character = '-';
        else if (character == '/')
            character = '_';
    }
    while (!canonical.empty() && canonical.back() == '=')
        canonical.pop_back();

    if (std::string_view(canonical) != acEncoded)
    {
        aDecoded.clear();
        return false;
    }
    return true;
}

void AppendUtf8(std::string& aOutput, std::uint32_t aCodePoint)
{
    if (aCodePoint <= 0x7f)
        aOutput.push_back(static_cast<char>(aCodePoint));
    else if (aCodePoint <= 0x7ff)
    {
        aOutput.push_back(static_cast<char>(0xc0 | (aCodePoint >> 6)));
        aOutput.push_back(static_cast<char>(0x80 | (aCodePoint & 0x3f)));
    }
    else if (aCodePoint <= 0xffff)
    {
        aOutput.push_back(static_cast<char>(0xe0 | (aCodePoint >> 12)));
        aOutput.push_back(static_cast<char>(0x80 | ((aCodePoint >> 6) & 0x3f)));
        aOutput.push_back(static_cast<char>(0x80 | (aCodePoint & 0x3f)));
    }
    else
    {
        aOutput.push_back(static_cast<char>(0xf0 | (aCodePoint >> 18)));
        aOutput.push_back(static_cast<char>(0x80 | ((aCodePoint >> 12) & 0x3f)));
        aOutput.push_back(static_cast<char>(0x80 | ((aCodePoint >> 6) & 0x3f)));
        aOutput.push_back(static_cast<char>(0x80 | (aCodePoint & 0x3f)));
    }
}

bool ReadHex4(const std::string_view acInput, std::size_t& aPosition, std::uint32_t& aValue)
{
    if (aPosition + 4 > acInput.size())
        return false;
    aValue = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        const char digit = acInput[aPosition++];
        aValue <<= 4;
        if (digit >= '0' && digit <= '9')
            aValue |= static_cast<std::uint32_t>(digit - '0');
        else if (digit >= 'a' && digit <= 'f')
            aValue |= static_cast<std::uint32_t>(digit - 'a' + 10);
        else if (digit >= 'A' && digit <= 'F')
            aValue |= static_cast<std::uint32_t>(digit - 'A' + 10);
        else
            return false;
    }
    return true;
}

bool ReadJsonString(const std::string_view acInput, std::size_t& aPosition, std::string& aValue)
{
    if (aPosition >= acInput.size() || acInput[aPosition++] != '"')
        return false;
    aValue.clear();
    while (aPosition < acInput.size())
    {
        const auto character = static_cast<unsigned char>(acInput[aPosition++]);
        if (character == '"')
            return true;
        if (character < 0x20)
            return false;
        if (character != '\\')
        {
            aValue.push_back(static_cast<char>(character));
            continue;
        }
        if (aPosition >= acInput.size())
            return false;
        const char escaped = acInput[aPosition++];
        switch (escaped)
        {
        case '"': aValue.push_back('"'); break;
        case '\\': aValue.push_back('\\'); break;
        case '/': aValue.push_back('/'); break;
        case 'b': aValue.push_back('\b'); break;
        case 'f': aValue.push_back('\f'); break;
        case 'n': aValue.push_back('\n'); break;
        case 'r': aValue.push_back('\r'); break;
        case 't': aValue.push_back('\t'); break;
        case 'u':
        {
            std::uint32_t codePoint{};
            if (!ReadHex4(acInput, aPosition, codePoint))
                return false;
            if (codePoint >= 0xd800 && codePoint <= 0xdbff)
            {
                if (aPosition + 2 > acInput.size() || acInput[aPosition] != '\\' || acInput[aPosition + 1] != 'u')
                    return false;
                aPosition += 2;
                std::uint32_t low{};
                if (!ReadHex4(acInput, aPosition, low) || low < 0xdc00 || low > 0xdfff)
                    return false;
                codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
            }
            else if (codePoint >= 0xdc00 && codePoint <= 0xdfff)
                return false;
            AppendUtf8(aValue, codePoint);
            break;
        }
        default: return false;
        }
    }
    return false;
}

void SkipWhitespace(const std::string_view acInput, std::size_t& aPosition)
{
    while (aPosition < acInput.size() && (acInput[aPosition] == ' ' || acInput[aPosition] == '\t' || acInput[aPosition] == '\r' || acInput[aPosition] == '\n'))
        ++aPosition;
}

bool ReadJsonInteger(const std::string_view acInput, std::size_t& aPosition, std::int64_t& aValue)
{
    const std::size_t start = aPosition;
    if (aPosition < acInput.size() && acInput[aPosition] == '-')
        ++aPosition;
    const std::size_t digits = aPosition;
    while (aPosition < acInput.size() && acInput[aPosition] >= '0' && acInput[aPosition] <= '9')
        ++aPosition;
    if (digits == aPosition || (aPosition - digits > 1 && acInput[digits] == '0'))
        return false;
    const auto parsed = std::from_chars(acInput.data() + start, acInput.data() + aPosition, aValue);
    return parsed.ec == std::errc{} && parsed.ptr == acInput.data() + aPosition;
}

bool ParseFlatJsonObject(const std::string_view acInput, std::map<std::string, std::string>& aStrings,
    std::map<std::string, std::int64_t>& aIntegers)
{
    std::size_t position = 0;
    SkipWhitespace(acInput, position);
    if (position >= acInput.size() || acInput[position++] != '{')
        return false;
    SkipWhitespace(acInput, position);
    if (position < acInput.size() && acInput[position] == '}')
        return false;

    while (position < acInput.size())
    {
        std::string key;
        if (!ReadJsonString(acInput, position, key))
            return false;
        SkipWhitespace(acInput, position);
        if (position >= acInput.size() || acInput[position++] != ':')
            return false;
        SkipWhitespace(acInput, position);

        if (position < acInput.size() && acInput[position] == '"')
        {
            std::string value;
            if (!ReadJsonString(acInput, position, value) || !aStrings.emplace(std::move(key), std::move(value)).second)
                return false;
        }
        else
        {
            std::int64_t value{};
            if (!ReadJsonInteger(acInput, position, value) || !aIntegers.emplace(std::move(key), value).second)
                return false;
        }

        SkipWhitespace(acInput, position);
        if (position >= acInput.size())
            return false;
        if (acInput[position] == '}')
        {
            ++position;
            SkipWhitespace(acInput, position);
            return position == acInput.size();
        }
        if (acInput[position++] != ',')
            return false;
        SkipWhitespace(acInput, position);
    }
    return false;
}

bool ConstantTimeEquals(const std::string_view acLeft, const std::string_view acRight) noexcept
{
    if (acLeft.size() != acRight.size())
        return false;
    unsigned char difference = 0;
    for (std::size_t index = 0; index < acLeft.size(); ++index)
        difference |= static_cast<unsigned char>(acLeft[index] ^ acRight[index]);
    return difference == 0;
}

bool HasOnlyDigits(const std::string_view acValue) noexcept
{
    if (acValue.empty() || (acValue.size() > 1 && acValue.front() == '0'))
        return false;
    return std::all_of(acValue.begin(), acValue.end(), [](const char character) { return character >= '0' && character <= '9'; });
}
} // namespace

bool Auth::VerifySessionToken(const std::string_view acToken, const std::string_view acHmacSecret,
    SessionClaims& aClaims, std::string& aErrorKey) noexcept
{
    aErrorKey = "auth.token_invalid";
    if (acHmacSecret.size() < 32 || acToken.size() > 8192)
    {
        aErrorKey = acHmacSecret.size() < 32 ? "auth.server_misconfigured" : "auth.token_invalid";
        return false;
    }

    const std::size_t firstDot = acToken.find('.');
    const std::size_t secondDot = firstDot == std::string_view::npos ? firstDot : acToken.find('.', firstDot + 1);
    if (firstDot == std::string_view::npos || secondDot == std::string_view::npos || acToken.find('.', secondDot + 1) != std::string_view::npos)
        return false;

    std::string headerJson;
    std::string payloadJson;
    std::string signature;
    if (!DecodeBase64Url(acToken.substr(0, firstDot), headerJson) || !DecodeBase64Url(acToken.substr(firstDot + 1, secondDot - firstDot - 1), payloadJson) ||
        !DecodeBase64Url(acToken.substr(secondDot + 1), signature) || signature.size() != CryptoPP::SHA256::DIGESTSIZE)
        return false;

    try
    {
        std::array<CryptoPP::byte, CryptoPP::SHA256::DIGESTSIZE> expected{};
        CryptoPP::HMAC<CryptoPP::SHA256> hmac(reinterpret_cast<const CryptoPP::byte*>(acHmacSecret.data()), acHmacSecret.size());
        const std::string_view signingInput = acToken.substr(0, secondDot);
        hmac.CalculateDigest(expected.data(), reinterpret_cast<const CryptoPP::byte*>(signingInput.data()), signingInput.size());
        if (!ConstantTimeEquals(std::string_view(reinterpret_cast<const char*>(expected.data()), expected.size()), signature))
            return false;
    }
    catch (const CryptoPP::Exception&)
    {
        return false;
    }

    std::map<std::string, std::string> headerStrings;
    std::map<std::string, std::int64_t> headerIntegers;
    if (!ParseFlatJsonObject(headerJson, headerStrings, headerIntegers) || headerStrings.size() != 2 || headerIntegers.size() != 0 ||
        headerStrings["alg"] != "HS256" || headerStrings["typ"] != "JWT")
        return false;

    std::map<std::string, std::string> stringClaims;
    std::map<std::string, std::int64_t> integerClaims;
    if (!ParseFlatJsonObject(payloadJson, stringClaims, integerClaims))
        return false;
    const auto issuer = stringClaims.find("iss");
    const auto subject = stringClaims.find("sub");
    const auto name = stringClaims.find("name");
    const auto avatar = stringClaims.find("avatar");
    const auto issuedAt = integerClaims.find("iat");
    const auto expiresAt = integerClaims.find("exp");
    if (issuer == stringClaims.end() || issuer->second != "sos-auth" || subject == stringClaims.end() || name == stringClaims.end() ||
        avatar == stringClaims.end() || issuedAt == integerClaims.end() || expiresAt == integerClaims.end() ||
        subject->second.rfind("discord:", 0) != 0 || name->second.empty() || name->second.size() > 128 || avatar->second.size() > 512)
        return false;

    const std::string_view discordId(subject->second.data() + 8, subject->second.size() - 8);
    if (!HasOnlyDigits(discordId))
        return false;
    std::uint64_t numericId{};
    const auto parsedId = std::from_chars(discordId.data(), discordId.data() + discordId.size(), numericId);
    if (parsedId.ec != std::errc{} || parsedId.ptr != discordId.data() + discordId.size() || numericId == 0)
        return false;

    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (issuedAt->second < 0 || expiresAt->second <= now || issuedAt->second > now + 60 ||
        issuedAt->second > std::numeric_limits<std::int64_t>::max() - kMaxTokenLifetimeSeconds ||
        expiresAt->second <= issuedAt->second || expiresAt->second > issuedAt->second + kMaxTokenLifetimeSeconds)
    {
        aErrorKey = "auth.token_expired";
        return false;
    }

    aClaims.DiscordId = numericId;
    aClaims.DisplayName = name->second;
    aClaims.AvatarUrl = avatar->second;
    aClaims.ExpiresAt = expiresAt->second;
    aErrorKey.clear();
    return true;
}
