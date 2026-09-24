#include <Messages/CombatHitObservationRequest.h>

#include <limits>

void CombatHitObservationRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, AttackerServerId);
    Serialization::WriteVarInt(aWriter, AttackerOwnershipEpoch);
    Serialization::WriteVarInt(aWriter, TargetServerId);
    Serialization::WriteVarInt(aWriter, TargetLifecycleGeneration);
    Serialization::WriteVarInt(aWriter, ObservationId);
}

void CombatHitObservationRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    bool hasValidWireEncoding = true;
    const auto readUInt32 = [&aReader, &hasValidWireEncoding]() noexcept {
        const auto value = Serialization::ReadVarInt(aReader);
        if (value > std::numeric_limits<std::uint32_t>::max())
        {
            hasValidWireEncoding = false;
            return std::uint32_t{};
        }

        return static_cast<std::uint32_t>(value);
    };

    AttackerServerId = readUInt32();
    AttackerOwnershipEpoch = readUInt32();
    TargetServerId = readUInt32();
    TargetLifecycleGeneration = Serialization::ReadVarInt(aReader);
    ObservationId = Serialization::ReadVarInt(aReader);
    m_hasValidWireEncoding = hasValidWireEncoding;
}
