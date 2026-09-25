#pragma once

#include <cstdint>

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Stl.hpp>

#include <Structs/GameId.h>

/**
 * @brief Server-authoritative character data delivered after character selection.
 *
 * This is deliberately separate from Persistence::CharacterRecord so the network
 * protocol does not expose ownership or database metadata.
 */
struct CharacterLoadSnapshot final
{
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const CharacterLoadSnapshot& acRhs) const noexcept;
    bool operator!=(const CharacterLoadSnapshot& acRhs) const noexcept { return !(*this == acRhs); }

    std::uint64_t CharacterId{};
    TiltedPhoques::String Name;
    GameId Race{};
    std::int32_t Sex{};
    std::int32_t Level{};
    GameId WorldSpaceId{};
    GameId CellId{};
    float PositionX{};
    float PositionY{};
    float PositionZ{};
    float Health{};
    float Magicka{};
    float Stamina{};
    bool NeedsRaceMenu{};
    // Stored RaceMenu look (TESNPC save buffer); empty until the character finished RaceMenu once.
    std::uint32_t AppearanceChangeFlags{};
    TiltedPhoques::String Appearance;
};
