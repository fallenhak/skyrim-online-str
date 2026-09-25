#include <Messages/UpdatePlayerAppearanceRequest.h>

#include <TiltedCore/Serialization.hpp>

void UpdatePlayerAppearanceRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(ChangeFlags, 32);
    TiltedPhoques::Serialization::WriteString(aWriter, AppearanceBuffer);
    FaceTints.Serialize(aWriter);
}

void UpdatePlayerAppearanceRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    uint64_t flags = 0;
    aReader.ReadBits(flags, 32);
    ChangeFlags = flags & 0xFFFFFFFF;
    AppearanceBuffer = TiltedPhoques::Serialization::ReadString(aReader);
    FaceTints = {};
    FaceTints.Deserialize(aReader);
}
