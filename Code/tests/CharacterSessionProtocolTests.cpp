#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <catch2/catch.hpp>

#include <Messages/ClientMessageFactory.h>
#include <Messages/CharacterReadyRequest.h>
#include <Messages/NotifyCharacterLoadSnapshot.h>
#include <Messages/NotifyCharacterEnteredWorld.h>
#include <Messages/NotifyCharacterAssignmentRejected.h>
#include <Messages/NotifyCharacterList.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyCharacterReadyResult.h>
#include <Messages/NotifyCharacterSelectionResult.h>
#include <Messages/RequestCharacterList.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/SelectCharacterRequest.h>
#include <Messages/ServerMessageFactory.h>

#include <Structs/CharacterLoadSnapshotValidation.h>
#include <Structs/CharacterSessionOutboundPolicy.h>

#include <limits>

template <typename T>
concept HasOwnerProfileId = requires(T aSnapshot) {
    aSnapshot.OwnerProfileId;
};

static_assert(!HasOwnerProfileId<CharacterLoadSnapshot>);

TEST_CASE("Character load snapshot round trips all server-authoritative fields", "[encoding.character_load]")
{
    CharacterLoadSnapshot sent{};
    sent.CharacterId = std::numeric_limits<std::uint64_t>::max();
    sent.Name = "Persistent Dragonborn";
    sent.Race = GameId(0x01020304, 0x05060708);
    sent.Sex = 1;
    sent.Level = 42;
    sent.WorldSpaceId = GameId(0x11121314, 0x15161718);
    sent.CellId = GameId(0x21222324, 0x25262728);
    sent.PositionX = -123.5f;
    sent.PositionY = 456.25f;
    sent.PositionZ = 789.75f;
    sent.Health = 321.5f;
    sent.Magicka = 222.25f;
    sent.Stamina = 111.75f;

    TiltedPhoques::Buffer buffer(1024);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    CharacterLoadSnapshot received{};
    TiltedPhoques::Buffer::Reader reader(&buffer);
    received.Deserialize(reader);

    REQUIRE(received == sent);
}

TEST_CASE("Character session protocol messages round trip", "[encoding.character_session]")
{
    SECTION("client requests")
    {
        RequestCharacterList listRequest{};
        TiltedPhoques::Buffer listBuffer(256);
        TiltedPhoques::Buffer::Writer listWriter(&listBuffer);
        listRequest.Serialize(listWriter);

        TiltedPhoques::Buffer::Reader listReader(&listBuffer);
        const ClientMessageFactory clientFactory;
        auto listMessage = clientFactory.Extract(listReader);
        REQUIRE(listMessage);
        auto parsedListRequest = TiltedPhoques::CastUnique<RequestCharacterList>(std::move(listMessage));
        REQUIRE(*parsedListRequest == listRequest);

        SelectCharacterRequest selectionRequest{};
        selectionRequest.CharacterId = std::numeric_limits<std::uint64_t>::max();
        TiltedPhoques::Buffer selectionBuffer(256);
        TiltedPhoques::Buffer::Writer selectionWriter(&selectionBuffer);
        selectionRequest.Serialize(selectionWriter);

        TiltedPhoques::Buffer::Reader selectionReader(&selectionBuffer);
        auto selectionMessage = clientFactory.Extract(selectionReader);
        REQUIRE(selectionMessage);
        auto parsedSelectionRequest = TiltedPhoques::CastUnique<SelectCharacterRequest>(std::move(selectionMessage));
        REQUIRE(*parsedSelectionRequest == selectionRequest);

        CharacterReadyRequest readyRequest{};
        readyRequest.CharacterId = std::numeric_limits<std::uint64_t>::max();
        TiltedPhoques::Buffer readyBuffer(256);
        TiltedPhoques::Buffer::Writer readyWriter(&readyBuffer);
        readyRequest.Serialize(readyWriter);

        TiltedPhoques::Buffer::Reader readyReader(&readyBuffer);
        auto readyMessage = clientFactory.Extract(readyReader);
        REQUIRE(readyMessage);
        auto parsedReadyRequest = TiltedPhoques::CastUnique<CharacterReadyRequest>(std::move(readyMessage));
        REQUIRE(*parsedReadyRequest == readyRequest);

        RequestHealthChangeBroadcast healthRequest{};
        healthRequest.Id = 0x1234;
        healthRequest.DeltaHealth = -12.5f;
        healthRequest.OwnershipEpoch = 17;
        TiltedPhoques::Buffer healthBuffer(256);
        TiltedPhoques::Buffer::Writer healthWriter(&healthBuffer);
        healthRequest.Serialize(healthWriter);

        TiltedPhoques::Buffer::Reader healthReader(&healthBuffer);
        auto healthMessage = clientFactory.Extract(healthReader);
        REQUIRE(healthMessage);
        auto parsedHealthRequest = TiltedPhoques::CastUnique<RequestHealthChangeBroadcast>(std::move(healthMessage));
        REQUIRE(*parsedHealthRequest == healthRequest);
    }

    SECTION("server list and selection result")
    {
        NotifyCharacterList list{};
        list.Characters = {
            CharacterSummary{42, "Aela", GameId(0x01, 0x00013746), 0, 18},
            CharacterSummary{std::numeric_limits<std::uint64_t>::max(), "O'Reilly", GameId(0x02, 0x0000003c), 1, 27}};

        TiltedPhoques::Buffer listBuffer(1024);
        TiltedPhoques::Buffer::Writer listWriter(&listBuffer);
        list.Serialize(listWriter);

        TiltedPhoques::Buffer::Reader listReader(&listBuffer);
        const ServerMessageFactory serverFactory;
        auto listMessage = serverFactory.Extract(listReader);
        REQUIRE(listMessage);
        auto parsedList = TiltedPhoques::CastUnique<NotifyCharacterList>(std::move(listMessage));
        REQUIRE(*parsedList == list);

        NotifyCharacterSelectionResult result{};
        result.Status = CharacterSelectionStatus::kNotFoundOrNotOwned;
        TiltedPhoques::Buffer resultBuffer(256);
        TiltedPhoques::Buffer::Writer resultWriter(&resultBuffer);
        result.Serialize(resultWriter);

        TiltedPhoques::Buffer::Reader resultReader(&resultBuffer);
        auto resultMessage = serverFactory.Extract(resultReader);
        REQUIRE(resultMessage);
        auto parsedResult = TiltedPhoques::CastUnique<NotifyCharacterSelectionResult>(std::move(resultMessage));
        REQUIRE(*parsedResult == result);

        NotifyCharacterLoadSnapshot snapshotMessage{};
        snapshotMessage.Snapshot.CharacterId = 0x7FFFFFFFFFFFFFFF;
        snapshotMessage.Snapshot.Name = "Snapshot";
        snapshotMessage.Snapshot.Race = GameId(0x01, 0x00013746);
        snapshotMessage.Snapshot.WorldSpaceId = GameId(0x02, 0x0000003C);
        snapshotMessage.Snapshot.CellId = GameId(0x03, 0x0000004D);
        snapshotMessage.Snapshot.PositionX = 10.5f;
        snapshotMessage.Snapshot.PositionY = -20.25f;
        snapshotMessage.Snapshot.PositionZ = 30.75f;
        snapshotMessage.Snapshot.Health = 100.0f;
        snapshotMessage.Snapshot.Magicka = 80.0f;
        snapshotMessage.Snapshot.Stamina = 60.0f;

        TiltedPhoques::Buffer snapshotBuffer(1024);
        TiltedPhoques::Buffer::Writer snapshotWriter(&snapshotBuffer);
        snapshotMessage.Serialize(snapshotWriter);

        TiltedPhoques::Buffer::Reader snapshotReader(&snapshotBuffer);
        auto snapshotNetworkMessage = serverFactory.Extract(snapshotReader);
        REQUIRE(snapshotNetworkMessage);
        auto parsedSnapshot = TiltedPhoques::CastUnique<NotifyCharacterLoadSnapshot>(std::move(snapshotNetworkMessage));
        REQUIRE(*parsedSnapshot == snapshotMessage);

        NotifyCharacterReadyResult readyResult{};
        readyResult.Status = CharacterReadyStatus::kProceed;
        TiltedPhoques::Buffer readyResultBuffer(256);
        TiltedPhoques::Buffer::Writer readyResultWriter(&readyResultBuffer);
        readyResult.Serialize(readyResultWriter);

        TiltedPhoques::Buffer::Reader readyResultReader(&readyResultBuffer);
        auto readyResultMessage = serverFactory.Extract(readyResultReader);
        REQUIRE(readyResultMessage);
        auto parsedReadyResult = TiltedPhoques::CastUnique<NotifyCharacterReadyResult>(std::move(readyResultMessage));
        REQUIRE(*parsedReadyResult == readyResult);

        NotifyCharacterEnteredWorld enteredWorld{};
        enteredWorld.CharacterId = std::numeric_limits<std::uint64_t>::max();
        TiltedPhoques::Buffer enteredWorldBuffer(256);
        TiltedPhoques::Buffer::Writer enteredWorldWriter(&enteredWorldBuffer);
        enteredWorld.Serialize(enteredWorldWriter);

        TiltedPhoques::Buffer::Reader enteredWorldReader(&enteredWorldBuffer);
        auto enteredWorldMessage = serverFactory.Extract(enteredWorldReader);
        REQUIRE(enteredWorldMessage);
        auto parsedEnteredWorld = TiltedPhoques::CastUnique<NotifyCharacterEnteredWorld>(std::move(enteredWorldMessage));
        REQUIRE(*parsedEnteredWorld == enteredWorld);

        NotifyHealthChangeBroadcast healthNotification{};
        healthNotification.Id = 0x5678;
        healthNotification.DeltaHealth = 8.25f;
        healthNotification.OwnershipEpoch = 23;
        TiltedPhoques::Buffer healthNotificationBuffer(256);
        TiltedPhoques::Buffer::Writer healthNotificationWriter(&healthNotificationBuffer);
        healthNotification.Serialize(healthNotificationWriter);

        TiltedPhoques::Buffer::Reader healthNotificationReader(&healthNotificationBuffer);
        auto healthNotificationMessage = serverFactory.Extract(healthNotificationReader);
        REQUIRE(healthNotificationMessage);
        auto parsedHealthNotification = TiltedPhoques::CastUnique<NotifyHealthChangeBroadcast>(std::move(healthNotificationMessage));
        REQUIRE(*parsedHealthNotification == healthNotification);

        NotifyCharacterAssignmentRejected rejection{};
        rejection.Cookie = std::numeric_limits<std::uint32_t>::max();
        rejection.Reason = CharacterAssignmentRejectReason::kPopulationHumanoidDenied;
        TiltedPhoques::Buffer rejectionBuffer(256);
        TiltedPhoques::Buffer::Writer rejectionWriter(&rejectionBuffer);
        rejection.Serialize(rejectionWriter);

        TiltedPhoques::Buffer::Reader rejectionReader(&rejectionBuffer);
        auto rejectionNetworkMessage = serverFactory.Extract(rejectionReader);
        REQUIRE(rejectionNetworkMessage);
        auto parsedRejection = TiltedPhoques::CastUnique<NotifyCharacterAssignmentRejected>(std::move(rejectionNetworkMessage));
        REQUIRE(*parsedRejection == rejection);
        REQUIRE(parsedRejection->Cookie == rejection.Cookie);
        REQUIRE(parsedRejection->Reason == CharacterAssignmentRejectReason::kPopulationHumanoidDenied);

        REQUIRE(static_cast<unsigned>(kNotifyCharacterAssignmentRejected) == static_cast<unsigned>(kNotifyProgressionAward) + 1);
        REQUIRE(static_cast<unsigned>(kServerOpcodeMax) == static_cast<unsigned>(kNotifyCharacterAssignmentRejected) + 1);
    }
}

TEST_CASE("Character load snapshot validation rejects unsafe values", "[encoding.character_load]")
{
    CharacterLoadSnapshot snapshot{};
    snapshot.CharacterId = 1;
    snapshot.Race = GameId(0, 0x00013746);
    snapshot.CellId = GameId(0, 0x0000003C);
    snapshot.Level = 20;
    snapshot.Sex = 0;
    snapshot.Health = 100.f;
    snapshot.Magicka = 100.f;
    snapshot.Stamina = 100.f;

    REQUIRE(IsCharacterLoadSnapshotValid(snapshot));

    snapshot.PositionX = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(ValidateCharacterLoadSnapshot(snapshot) == CharacterLoadSnapshotValidationError::kInvalidPosition);
    snapshot.PositionX = 0.f;
    snapshot.Health = std::numeric_limits<float>::infinity();
    REQUIRE(ValidateCharacterLoadSnapshot(snapshot) == CharacterLoadSnapshotValidationError::kInvalidVitals);
    snapshot.Health = 100.f;
    snapshot.Sex = 2;
    REQUIRE(ValidateCharacterLoadSnapshot(snapshot) == CharacterLoadSnapshotValidationError::kUnsupportedSex);
    snapshot.Sex = 0;
    snapshot.Race = GameId{};
    REQUIRE(ValidateCharacterLoadSnapshot(snapshot) == CharacterLoadSnapshotValidationError::kInvalidRace);
    snapshot.Race = GameId(0, 0x00013746);
    snapshot.CellId = GameId{};
    REQUIRE(ValidateCharacterLoadSnapshot(snapshot) == CharacterLoadSnapshotValidationError::kInvalidCell);
}

TEST_CASE("Character outbound protocol policy keeps pre-world traffic narrow", "[encoding.character_session]")
{
    REQUIRE(!CanSendCharacterProtocolMessage(kRequestActorValueChanges, CharacterClientSessionPhase::kAwaitingClientReady, false));
    REQUIRE(CanSendCharacterProtocolMessage(kRequestCharacterList, CharacterClientSessionPhase::kAwaitingCharacterSelection, false));
    REQUIRE(CanSendCharacterProtocolMessage(kCharacterReadyRequest, CharacterClientSessionPhase::kAwaitingClientReady, false));
    REQUIRE(!CanSendCharacterProtocolMessage(kAssignCharacterRequest, CharacterClientSessionPhase::kAwaitingPlayerAssignment, false));
    REQUIRE(CanSendCharacterProtocolMessage(kAssignCharacterRequest, CharacterClientSessionPhase::kAwaitingPlayerAssignment, true));
    REQUIRE(CanSendCharacterProtocolMessage(kRequestActorValueChanges, CharacterClientSessionPhase::kInWorld, false));
}
