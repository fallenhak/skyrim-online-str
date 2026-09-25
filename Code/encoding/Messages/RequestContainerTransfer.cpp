#include <Messages/RequestContainerTransfer.h>
#include <TiltedCore/Serialization.hpp>

void RequestContainerTransfer::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteVarInt(aWriter, ContainerId);
    aWriter.WriteBits(TargetKind, 8);
    aWriter.WriteBits(Direction, 8);
    Serialization::WriteVarInt(aWriter, static_cast<uint32_t>(ExpectedContainerCount));
    Item.Serialize(aWriter);
}

void RequestContainerTransfer::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    RequestId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ContainerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    uint64_t targetKind = 0;
    aReader.ReadBits(targetKind, 8);
    TargetKind = static_cast<uint8_t>(targetKind);
    uint64_t direction = 0;
    aReader.ReadBits(direction, 8);
    Direction = static_cast<uint8_t>(direction);
    ExpectedContainerCount = static_cast<int32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);
    Item.Deserialize(aReader);
}
