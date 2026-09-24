#pragma once

#include <TiltedCore/Stl.hpp>

#include "Faction.h"

#include <cstddef>

using TiltedPhoques::Vector;

struct Factions
{
    static constexpr std::size_t kMaxEntriesPerList = 0x1FF;

    Factions() = default;
    ~Factions() = default;

    bool operator==(const Factions& acRhs) const noexcept;
    bool operator!=(const Factions& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    Vector<Faction> NpcFactions;
    Vector<Faction> ExtraFactions;
};

struct FactionUpdate final
{
    bool operator==(const FactionUpdate& acRhs) const noexcept
    {
        return OwnershipEpoch == acRhs.OwnershipEpoch && FactionsContent == acRhs.FactionsContent;
    }

    bool operator!=(const FactionUpdate& acRhs) const noexcept { return !operator==(acRhs); }

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t OwnershipEpoch{};
    Factions FactionsContent{};
};
