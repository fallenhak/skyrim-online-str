#include <Structs/ActionReplayChain.h>

using TiltedPhoques::Serialization;

namespace
{
// Bounds a malformed count; the replay cache is trimmed well below this.
constexpr uint64_t kMaxReplayActions = 1024;
}

bool ActionReplayChain::operator==(const ActionReplayChain& acRhs) const noexcept
{
    return ResetAnimationGraph == acRhs.ResetAnimationGraph && Actions == acRhs.Actions;
}

bool ActionReplayChain::operator!=(const ActionReplayChain& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ActionReplayChain::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, ResetAnimationGraph);
    Serialization::WriteVarInt(aWriter, Actions.size());
    for (size_t i = 0; i < Actions.size(); ++i)
    {
        Actions[i].GenerateDifferential(ActionEvent{}, aWriter);
    }
}

void ActionReplayChain::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ResetAnimationGraph = Serialization::ReadBool(aReader);
    const uint64_t actionsCount = Serialization::ReadVarInt(aReader);
    if (actionsCount > kMaxReplayActions)
        return;
    Actions.resize(actionsCount);
    for (ActionEvent& replayAction : Actions)
    {
        replayAction.ApplyDifferential(aReader);
    }
}
