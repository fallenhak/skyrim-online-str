#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>

#include <TiltedCore/Stl.hpp>

namespace ESLoader
{
inline bool IsAsciiWhitespace(const char aCharacter) noexcept
{
    switch (aCharacter)
    {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\f':
    case '\v': return true;
    default: return false;
    }
}

inline char ToAsciiLower(const char aCharacter) noexcept
{
    return aCharacter >= 'A' && aCharacter <= 'Z' ? static_cast<char>(aCharacter - 'A' + 'a') : aCharacter;
}

inline bool IsAsciiControl(const char aCharacter) noexcept
{
    const auto value = static_cast<unsigned char>(aCharacter);
    return value < 0x20 || value == 0x7F;
}

inline bool IsValidUtf8(const TiltedPhoques::String& acValue) noexcept
{
    std::size_t index = 0;
    while (index < acValue.size())
    {
        const auto lead = static_cast<unsigned char>(acValue[index]);
        if (lead <= 0x7F)
        {
            ++index;
            continue;
        }

        std::size_t continuationCount = 0;
        if (lead >= 0xC2 && lead <= 0xDF)
            continuationCount = 1;
        else if (lead >= 0xE0 && lead <= 0xEF)
            continuationCount = 2;
        else if (lead >= 0xF0 && lead <= 0xF4)
            continuationCount = 3;
        else
            return false;

        if (continuationCount >= acValue.size() - index)
            return false;

        const auto second = static_cast<unsigned char>(acValue[index + 1]);
        if ((second & 0xC0) != 0x80 || (lead == 0xE0 && second < 0xA0) || (lead == 0xED && second >= 0xA0) ||
            (lead == 0xF0 && second < 0x90) || (lead == 0xF4 && second > 0x8F))
        {
            return false;
        }

        for (std::size_t continuation = 2; continuation <= continuationCount; ++continuation)
        {
            if ((static_cast<unsigned char>(acValue[index + continuation]) & 0xC0) != 0x80)
                return false;
        }

        index += continuationCount + 1;
    }

    return true;
}

// Filesystem paths use the platform's native representation internally, while
// plugin names in loadorder.txt and TES4 records are UTF-8 byte strings.
inline std::filesystem::path PathFromUtf8(const TiltedPhoques::String& acUtf8Path)
{
    std::u8string utf8Path;
    utf8Path.reserve(acUtf8Path.size());
    for (const char character : acUtf8Path)
        utf8Path.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    return std::filesystem::path(utf8Path);
}

inline TiltedPhoques::String PathToUtf8String(const std::filesystem::path& acPath)
{
    const std::u8string utf8Path = acPath.u8string();
    return TiltedPhoques::String(reinterpret_cast<const char*>(utf8Path.data()), utf8Path.size());
}

// Plugin names cross several case-sensitive boundaries: loadorder.txt, TES4
// master names, the filesystem and the network ModsComponent. Use this key for
// identity comparisons while retaining the original spelling for display.
inline bool GetPluginFilenameKey(const TiltedPhoques::String& acFilename, TiltedPhoques::String& aKey)
{
    auto first = acFilename.begin();
    while (first != acFilename.end() && IsAsciiWhitespace(*first))
        ++first;

    auto last = acFilename.end();
    while (last != first && IsAsciiWhitespace(*(last - 1)))
        --last;

    aKey.assign(first, last);
    if (aKey.empty() || !IsValidUtf8(aKey) || std::any_of(aKey.begin(), aKey.end(), [](const char aCharacter) {
            return aCharacter == '/' || aCharacter == '\\' || aCharacter == ':' ||
                   IsAsciiControl(aCharacter);
        }))
    {
        aKey.clear();
        return false;
    }

    std::transform(aKey.begin(), aKey.end(), aKey.begin(), [](const char aCharacter) {
        return ToAsciiLower(aCharacter);
    });
    return true;
}

inline bool ArePluginFilenamesEqual(const TiltedPhoques::String& acLeft, const TiltedPhoques::String& acRight)
{
    TiltedPhoques::String leftKey;
    TiltedPhoques::String rightKey;
    return GetPluginFilenameKey(acLeft, leftKey) && GetPluginFilenameKey(acRight, rightKey) && leftKey == rightKey;
}
} // namespace ESLoader
