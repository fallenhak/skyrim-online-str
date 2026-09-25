#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * Stable storage format for a player's look (RaceMenu result): change flags, the NPC appearance
 * buffer and the face tints. The wire Tints format writes string-cache ids, which do not survive
 * a restart, so the database gets this self-contained little-endian layout instead:
 *   "LK1" | u32 flags | u32 len | appearance bytes | u8 count | count * (u32 type, u32 color, u32 alpha bits, u16 len, name bytes)
 */
namespace CharacterLookCodec
{
struct TintEntry final
{
    std::uint32_t Type{};
    std::uint32_t Color{};
    float Alpha{};
    std::string Name;

    bool operator==(const TintEntry&) const = default;
};

struct Look final
{
    std::uint32_t ChangeFlags{};
    std::string Appearance;
    std::vector<TintEntry> Tints;

    bool operator==(const Look&) const = default;
};

inline constexpr std::size_t kMaxAppearanceBytes = 64 * 1024;
inline constexpr std::size_t kMaxTints = 255;
inline constexpr std::size_t kMaxTintNameBytes = 1024;

[[nodiscard]] inline bool IsWithinLimits(const Look& acLook) noexcept
{
    if (acLook.Appearance.empty() || acLook.Appearance.size() > kMaxAppearanceBytes || acLook.Tints.size() > kMaxTints)
        return false;
    for (const auto& tint : acLook.Tints)
    {
        if (tint.Name.size() > kMaxTintNameBytes)
            return false;
    }
    return true;
}

namespace Detail
{
inline void PutU32(std::string& aOut, const std::uint32_t aValue)
{
    for (int i = 0; i < 4; ++i)
        aOut.push_back(static_cast<char>((aValue >> (8 * i)) & 0xFF));
}

inline bool GetU32(std::string_view& aIn, std::uint32_t& aValue) noexcept
{
    if (aIn.size() < 4)
        return false;
    aValue = 0;
    for (int i = 0; i < 4; ++i)
        aValue |= static_cast<std::uint32_t>(static_cast<unsigned char>(aIn[i])) << (8 * i);
    aIn.remove_prefix(4);
    return true;
}
} // namespace Detail

[[nodiscard]] inline std::optional<std::string> Encode(const Look& acLook)
{
    if (!IsWithinLimits(acLook))
        return std::nullopt;

    std::string out = "LK1";
    Detail::PutU32(out, acLook.ChangeFlags);
    Detail::PutU32(out, static_cast<std::uint32_t>(acLook.Appearance.size()));
    out += acLook.Appearance;
    out.push_back(static_cast<char>(acLook.Tints.size()));
    for (const auto& tint : acLook.Tints)
    {
        std::uint32_t alphaBits = 0;
        std::memcpy(&alphaBits, &tint.Alpha, sizeof(alphaBits));
        Detail::PutU32(out, tint.Type);
        Detail::PutU32(out, tint.Color);
        Detail::PutU32(out, alphaBits);
        out.push_back(static_cast<char>(tint.Name.size() & 0xFF));
        out.push_back(static_cast<char>((tint.Name.size() >> 8) & 0xFF));
        out += tint.Name;
    }
    return out;
}

[[nodiscard]] inline std::optional<Look> Decode(std::string_view aIn)
{
    if (aIn.substr(0, 3) != "LK1")
        return std::nullopt;
    aIn.remove_prefix(3);

    Look look;
    std::uint32_t appearanceSize = 0;
    if (!Detail::GetU32(aIn, look.ChangeFlags) || !Detail::GetU32(aIn, appearanceSize) || appearanceSize > kMaxAppearanceBytes || aIn.size() < appearanceSize)
        return std::nullopt;
    look.Appearance.assign(aIn.substr(0, appearanceSize));
    aIn.remove_prefix(appearanceSize);

    if (aIn.empty())
        return std::nullopt;
    const std::size_t count = static_cast<unsigned char>(aIn[0]);
    aIn.remove_prefix(1);
    for (std::size_t i = 0; i < count; ++i)
    {
        TintEntry tint;
        std::uint32_t alphaBits = 0;
        if (!Detail::GetU32(aIn, tint.Type) || !Detail::GetU32(aIn, tint.Color) || !Detail::GetU32(aIn, alphaBits) || aIn.size() < 2)
            return std::nullopt;
        std::memcpy(&tint.Alpha, &alphaBits, sizeof(alphaBits));
        const std::size_t nameSize = static_cast<unsigned char>(aIn[0]) | (static_cast<std::size_t>(static_cast<unsigned char>(aIn[1])) << 8);
        aIn.remove_prefix(2);
        if (nameSize > kMaxTintNameBytes || aIn.size() < nameSize)
            return std::nullopt;
        tint.Name.assign(aIn.substr(0, nameSize));
        aIn.remove_prefix(nameSize);
        look.Tints.push_back(std::move(tint));
    }

    if (!aIn.empty() || !IsWithinLimits(look))
        return std::nullopt;
    return look;
}

// The database column is TEXT: hex keeps the binary appearance buffer intact through it.
[[nodiscard]] inline std::string ToHex(const std::string_view acBytes)
{
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(acBytes.size() * 2);
    for (const char c : acBytes)
    {
        const auto value = static_cast<unsigned char>(c);
        out.push_back(kDigits[value >> 4]);
        out.push_back(kDigits[value & 0x0F]);
    }
    return out;
}

[[nodiscard]] inline std::optional<std::string> FromHex(const std::string_view acHex)
{
    if (acHex.size() % 2 != 0)
        return std::nullopt;
    const auto nibble = [](const char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(acHex.size() / 2);
    for (std::size_t i = 0; i < acHex.size(); i += 2)
    {
        const int high = nibble(acHex[i]);
        const int low = nibble(acHex[i + 1]);
        if (high < 0 || low < 0)
            return std::nullopt;
        out.push_back(static_cast<char>((high << 4) | low));
    }
    return out;
}
} // namespace CharacterLookCodec
