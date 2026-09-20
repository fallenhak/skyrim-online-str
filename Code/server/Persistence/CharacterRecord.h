#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <TiltedCore/Buffer.hpp>

#include <Structs/GameId.h>

namespace Persistence
{
using CharacterId = std::int64_t;
using OwnerProfileId = std::string;

struct CharacterRecord final
{
    CharacterId Id{};
    Persistence::OwnerProfileId OwnerProfileId;
    std::string Name;
    GameId Race{};
    std::int32_t Sex{};
    std::int32_t Level{};
    GameId WorldSpace{};
    GameId Cell{};
    float PositionX{};
    float PositionY{};
    float PositionZ{};
    float Health{};
    float Magicka{};
    float Stamina{};
    std::int64_t CreatedAt{};
    std::int64_t UpdatedAt{};
};
} // namespace Persistence
