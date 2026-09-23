#include <Structs/Movement.h>
#include <TiltedCore/Serialization.hpp>
#include <cmath>

using TiltedPhoques::Serialization;

bool Movement::operator==(const Movement& acRhs) const noexcept
{
    return CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && Rotation == acRhs.Rotation && Variables == acRhs.Variables && Direction == acRhs.Direction;
}

bool Movement::operator!=(const Movement& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Movement::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Rotation.Serialize(aWriter);
    Variables.GenerateDiff(AnimationVariables{}, aWriter);
    aWriter.WriteBits(*reinterpret_cast<const uint32_t*>(&Direction), 32);
}

bool Movement::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Rotation.Deserialize(aReader);
    Variables = AnimationVariables{};
    if (!Variables.ApplyDiff(aReader))
        return false;

    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 32);
    uint32_t tmp32 = tmp & 0xFFFFFFFF;
    Direction = *reinterpret_cast<float*>(&tmp32);

    return std::isfinite(Position.x) && std::isfinite(Position.y) && std::isfinite(Position.z) && std::isfinite(Rotation.x) &&
           std::isfinite(Rotation.y) && std::isfinite(Direction);
}
