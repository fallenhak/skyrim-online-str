#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/GridCellCoords.h>

using TiltedPhoques::Vector;

struct ShiftGridCellRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kShiftGridCellRequest;

    ShiftGridCellRequest()
        : ClientMessage(Opcode)
    {
    }

    virtual ~ShiftGridCellRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ShiftGridCellRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && WorldSpaceId == acRhs.WorldSpaceId && PlayerCell == acRhs.PlayerCell && CenterCoords == acRhs.CenterCoords && Cells == acRhs.Cells; }

    GameId WorldSpaceId;
    GameId PlayerCell;
    GridCellCoords CenterCoords;
    Vector<GameId> Cells;
    // Wire count refused by the read bound; 0 when the list was read. Not serialized: the
    // server logs and drops such a message instead of handling it as an empty list.
    uint64_t OverLimitCount{};
};
