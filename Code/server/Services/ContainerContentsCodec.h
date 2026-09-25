#pragma once

// Tests compile without EncodingPch.h, so pull in what Inventory.h expects.
#include <optional>

#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <Structs/Inventory.h>
#include <Services/CharacterLookCodec.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

// A container's server-owned contents as stored in container_contents.inventory: the wire
// serialization of its entries, hex encoded for the TEXT column. Only entries are kept; a
// container has no magic equipment.
namespace ContainerContentsCodec
{
// A chest with a few hundred stacks fits in a few tens of KiB; anything near this is corrupt.
inline constexpr std::size_t kMaxEncodedBytes = 1u << 20;

[[nodiscard]] inline std::optional<std::string> Encode(const Inventory& acContents)
{
    Inventory entriesOnly{};
    entriesOnly.Entries = acContents.Entries;

    // Writer::WriteBytes refuses to write past the end instead of growing, so retry larger
    // until the whole serialization fits with room to spare.
    for (std::size_t capacity = 4096; capacity <= kMaxEncodedBytes; capacity *= 2)
    {
        TiltedPhoques::Buffer buffer(capacity);
        TiltedPhoques::Buffer::Writer writer(&buffer);
        entriesOnly.Serialize(writer);
        const auto written = writer.GetBytePosition();
        if (written < capacity)
            return CharacterLookCodec::ToHex(std::string_view(reinterpret_cast<const char*>(buffer.GetData()), written));
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<Inventory> Decode(const std::string_view acHex)
{
    if (acHex.empty() || acHex.size() > kMaxEncodedBytes * 2)
        return std::nullopt;

    const auto bytes = CharacterLookCodec::FromHex(acHex);
    if (!bytes)
        return std::nullopt;

    TiltedPhoques::Buffer buffer(bytes->size());
    std::memcpy(buffer.GetWriteData(), bytes->data(), bytes->size());
    TiltedPhoques::Buffer::Reader reader(&buffer);

    Inventory contents{};
    contents.Deserialize(reader);

    // A truncated or padded row must not load as a partial chest.
    if (reader.GetBytePosition() != bytes->size())
        return std::nullopt;

    for (const auto& entry : contents.Entries)
    {
        if (entry.Count <= 0 || !entry.BaseId)
            return std::nullopt;
    }

    // The reader clamps at the end of the buffer instead of failing, so a row cut short can still
    // end exactly at its size. Only a row that re-encodes to itself is a whole chest.
    const auto reencoded = Encode(contents);
    if (!reencoded || *reencoded != acHex)
        return std::nullopt;
    return contents;
}
} // namespace ContainerContentsCodec
