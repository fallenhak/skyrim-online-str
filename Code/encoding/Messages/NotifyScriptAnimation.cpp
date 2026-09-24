#include <Messages/NotifyScriptAnimation.h>

void NotifyScriptAnimation::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    FormID.Serialize(aWriter);
    Animation.Serialize(aWriter);
    EventName.Serialize(aWriter);
}

void NotifyScriptAnimation::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    FormID.Deserialize(aReader);
    Animation.Deserialize(aReader);
    EventName.Deserialize(aReader);
}
