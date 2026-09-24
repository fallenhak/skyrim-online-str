#include <Services/CharacterNamePolicy.h>

#include <cstdint>

namespace
{
bool DecodeUtf8(const std::string_view acText, std::size_t& aOffset, std::uint32_t& aCodePoint) noexcept
{
    if (aOffset >= acText.size())
        return false;

    const auto first = static_cast<std::uint8_t>(acText[aOffset++]);
    if (first < 0x80)
    {
        aCodePoint = first;
        return true;
    }

    std::uint32_t value{};
    std::size_t continuationCount{};
    if ((first & 0xE0) == 0xC0)
    {
        value = first & 0x1F;
        continuationCount = 1;
        if (value < 2)
            return false;
    }
    else if ((first & 0xF0) == 0xE0)
    {
        value = first & 0x0F;
        continuationCount = 2;
    }
    else if ((first & 0xF8) == 0xF0)
    {
        value = first & 0x07;
        continuationCount = 3;
        if (value > 4)
            return false;
    }
    else
    {
        return false;
    }

    if (aOffset + continuationCount > acText.size())
        return false;

    for (std::size_t index = 0; index < continuationCount; ++index)
    {
        const auto next = static_cast<std::uint8_t>(acText[aOffset++]);
        if ((next & 0xC0) != 0x80)
            return false;
        value = (value << 6) | (next & 0x3F);
    }

    if ((continuationCount == 2 && value < 0x800) || (continuationCount == 3 && value < 0x10000) || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
        return false;

    aCodePoint = value;
    return true;
}

bool IsLetter(const std::uint32_t aCodePoint) noexcept
{
    if ((aCodePoint >= 'A' && aCodePoint <= 'Z') || (aCodePoint >= 'a' && aCodePoint <= 'z'))
        return true;

    // Latin-1 letters (excluding the multiplication and division signs) include
    // all Turkish letters: Çç, Ğğ, İı, Öö, Şş and Üü.
    if ((aCodePoint >= 0x00C0 && aCodePoint <= 0x00D6) || (aCodePoint >= 0x00D8 && aCodePoint <= 0x00F6) || (aCodePoint >= 0x00F8 && aCodePoint <= 0x00FF))
        return true;

    // Latin Extended-A/B and Latin Extended Additional are letter blocks used
    // by European names. Exclude the few punctuation/combining code points.
    if ((aCodePoint >= 0x0100 && aCodePoint <= 0x024F) || (aCodePoint >= 0x1E00 && aCodePoint <= 0x1EFF))
        return true;

    return false;
}

std::uint32_t SimpleLowercase(std::uint32_t aCodePoint) noexcept
{
    if (aCodePoint >= 'A' && aCodePoint <= 'Z')
        return aCodePoint + 0x20;
    if ((aCodePoint >= 0x00C0 && aCodePoint <= 0x00D6) || (aCodePoint >= 0x00D8 && aCodePoint <= 0x00DE))
        return aCodePoint + 0x20;
    if (aCodePoint == 0x0178)
        return 0x00FF;
    if (aCodePoint == 0x0130)
        return 'i';

    // Most uppercase letters in the Latin Extended blocks alternate with the
    // lowercase letter at the following code point. A few irregular pairs are
    // handled explicitly below.
    if ((aCodePoint >= 0x0100 && aCodePoint <= 0x012F && (aCodePoint % 2 == 0)) || (aCodePoint >= 0x0132 && aCodePoint <= 0x0137 && (aCodePoint % 2 == 0)) ||
        (aCodePoint >= 0x014A && aCodePoint <= 0x0177 && (aCodePoint % 2 == 0)) || (aCodePoint >= 0x1E00 && aCodePoint <= 0x1EFF && (aCodePoint % 2 == 0)))
        return aCodePoint + 1;
    if (aCodePoint == 0x0139 || aCodePoint == 0x013B || aCodePoint == 0x013D || aCodePoint == 0x013F || aCodePoint == 0x0141 || aCodePoint == 0x0143 || aCodePoint == 0x0145 ||
        aCodePoint == 0x0147 || aCodePoint == 0x0179 || aCodePoint == 0x017B || aCodePoint == 0x017D)
        return aCodePoint + 1;
    return aCodePoint;
}

void AppendUtf8(std::string& aText, const std::uint32_t aCodePoint)
{
    if (aCodePoint < 0x80)
    {
        aText.push_back(static_cast<char>(aCodePoint));
    }
    else if (aCodePoint < 0x800)
    {
        aText.push_back(static_cast<char>(0xC0 | (aCodePoint >> 6)));
        aText.push_back(static_cast<char>(0x80 | (aCodePoint & 0x3F)));
    }
    else if (aCodePoint < 0x10000)
    {
        aText.push_back(static_cast<char>(0xE0 | (aCodePoint >> 12)));
        aText.push_back(static_cast<char>(0x80 | ((aCodePoint >> 6) & 0x3F)));
        aText.push_back(static_cast<char>(0x80 | (aCodePoint & 0x3F)));
    }
    else
    {
        aText.push_back(static_cast<char>(0xF0 | (aCodePoint >> 18)));
        aText.push_back(static_cast<char>(0x80 | ((aCodePoint >> 12) & 0x3F)));
        aText.push_back(static_cast<char>(0x80 | ((aCodePoint >> 6) & 0x3F)));
        aText.push_back(static_cast<char>(0x80 | (aCodePoint & 0x3F)));
    }
}
} // namespace

bool CharacterNamePolicy::IsValid(const std::string_view acName) noexcept
{
    std::size_t offset{};
    std::size_t characterCount{};
    while (offset < acName.size())
    {
        std::uint32_t codePoint{};
        if (!DecodeUtf8(acName, offset, codePoint))
            return false;

        if (!IsLetter(codePoint) && codePoint != ' ' && codePoint != '\'' && codePoint != '-')
            return false;

        if (++characterCount > 24)
            return false;
    }

    return characterCount >= 3;
}

std::string CharacterNamePolicy::MakeUniquenessKey(const std::string_view acName)
{
    std::string key;
    key.reserve(acName.size());
    std::size_t offset{};
    while (offset < acName.size())
    {
        std::uint32_t codePoint{};
        if (!DecodeUtf8(acName, offset, codePoint))
            return {};
        AppendUtf8(key, SimpleLowercase(codePoint));
    }
    return key;
}
