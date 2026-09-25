#pragma once

#include "Message.h"

// Answer to RequestContainerTransfer. Result 0 = accepted; anything else means the
// server did not apply the move and the sender rolls its local move back.
struct NotifyContainerTransferResult final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyContainerTransferResult;

    NotifyContainerTransferResult()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyContainerTransferResult& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && RequestId == acRhs.RequestId && Result == acRhs.Result; }

    uint32_t RequestId{};
    uint8_t Result{};
};
