#pragma once

#include <algorithm>
#include <cctype>

#include <TiltedCore/Stl.hpp>

namespace ESLoader
{
// Plugin names cross several case-sensitive boundaries: loadorder.txt, TES4
// master names, the filesystem and the network ModsComponent. Use this key for
// identity comparisons while retaining the original spelling for display.
inline bool GetPluginFilenameKey(const TiltedPhoques::String& acFilename, TiltedPhoques::String& aKey)
{
    const auto isWhitespace = [](const char aCharacter) {
        return std::isspace(static_cast<unsigned char>(aCharacter)) != 0;
    };

    auto first = acFilename.begin();
    while (first != acFilename.end() && isWhitespace(*first))
        ++first;

    auto last = acFilename.end();
    while (last != first && isWhitespace(*(last - 1)))
        --last;

    aKey.assign(first, last);
    if (aKey.empty() || std::any_of(aKey.begin(), aKey.end(), [](const char aCharacter) {
            return aCharacter == '/' || aCharacter == '\\' || aCharacter == ':' ||
                   std::iscntrl(static_cast<unsigned char>(aCharacter)) != 0;
        }))
    {
        aKey.clear();
        return false;
    }

    std::transform(aKey.begin(), aKey.end(), aKey.begin(), [](const char aCharacter) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(aCharacter)));
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
