#include <Messages/ShiftGridCellRequest.h>

namespace
{
// The loaded grid is at most uGridsToLoad squared; bounds a malformed count.
constexpr uint64_t kMaxGridCells = 1024;
}

void ShiftGridCellRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    WorldSpaceId.Serialize(aWriter);
    PlayerCell.Serialize(aWriter);
    CenterCoords.Serialize(aWriter);

    Serialization::WriteVarInt(aWriter, Cells.size());

    for (const auto& cell : Cells)
    {
        cell.Serialize(aWriter);
    }
}

void ShiftGridCellRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    WorldSpaceId.Deserialize(aReader);
    PlayerCell.Deserialize(aReader);
    CenterCoords.Deserialize(aReader);

    const uint64_t count = Serialization::ReadVarInt(aReader);
    if (count > kMaxGridCells)
        return;

    Cells.resize(count);

    for (auto i = 0u; i < count; ++i)
    {
        Cells[i].Deserialize(aReader);
    }
}
