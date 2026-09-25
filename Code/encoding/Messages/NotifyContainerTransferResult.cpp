#include <Messages/NotifyContainerTransferResult.h>
#include <TiltedCore/Serialization.hpp>

void NotifyContainerTransferResult::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, RequestId);
    aWriter.WriteBits(Result, 8);
}

void NotifyContainerTransferResult::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    RequestId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    uint64_t result = 0;
    aReader.ReadBits(result, 8);
    Result = static_cast<uint8_t>(result);
}
