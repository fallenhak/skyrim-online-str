#include <Messages/NotifyCharacterLoadSnapshot.h>

void NotifyCharacterLoadSnapshot::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Snapshot.Serialize(aWriter);
}

void NotifyCharacterLoadSnapshot::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    Snapshot.Deserialize(aReader);
}
