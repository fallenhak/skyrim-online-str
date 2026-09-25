#include <Messages/NotifyFurnitureUseDenied.h>

void NotifyFurnitureUseDenied::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(ActorId, 32);
    aWriter.WriteBits(OwnershipEpoch, 32);
    AuthoritativeMovement.Serialize(aWriter);
}

void NotifyFurnitureUseDenied::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    uint64_t value = 0;
    aReader.ReadBits(value, 32);
    ActorId = static_cast<uint32_t>(value);
    aReader.ReadBits(value, 32);
    OwnershipEpoch = static_cast<uint32_t>(value);
    AuthoritativeMovement.Deserialize(aReader);
}
