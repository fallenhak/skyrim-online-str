#pragma once

#include <TiltedCore/Buffer.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>

using TiltedPhoques::Buffer;

struct GameId
{
    GameId() = default;
    GameId(std::uint32_t aModId, std::uint32_t aBaseId) noexcept;
    ~GameId() = default;

    bool operator==(const GameId& acRhs) const noexcept;
    bool operator!=(const GameId& acRhs) const noexcept;

    operator bool() const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
    inline std::uint64_t LogFormat() const noexcept { return static_cast<std::uint64_t>(ModId) << 32 | BaseId; }

    std::uint32_t BaseId;
    std::uint32_t ModId;
};

namespace std
{
template <> class hash<GameId>
{
public:
    std::size_t operator()(const GameId& gameId) const
    {
        return hash<std::uint32_t>()(gameId.BaseId) ^ (hash<std::uint32_t>()(gameId.ModId) << 1);
    }
};
} // namespace std