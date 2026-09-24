#pragma once

#include <cstdint>

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Stl.hpp>

#include <Structs/GameId.h>

struct CharacterSummary final
{
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const CharacterSummary& acRhs) const noexcept;
    bool operator!=(const CharacterSummary& acRhs) const noexcept { return !(*this == acRhs); }

    std::uint64_t CharacterId{};
    TiltedPhoques::String Name;
    GameId Race{};
    std::int32_t Sex{};
    std::int32_t Level{};
    std::uint8_t SlotIndex{};
};
