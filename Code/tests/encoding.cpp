#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "StringCache.h"
#include "Messages/StringCacheUpdate.h"

#include <catch2/catch.hpp>

#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/RequestDeathStateChange.h>
#include <Messages/ObjectStateReport.h>
#include <Structs/Vector2_NetQuantize.h>

#include <TiltedCore/Math.hpp>
#include <TiltedCore/Platform.hpp>

using namespace TiltedPhoques;

TEST_CASE("Encoding factory", "[encoding.factory]")
{
    Buffer buff(1000);

    {
        AuthenticationRequest request;
        request.Token = "TesSt";

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<AuthenticationRequest>(std::move(pMessage));
        REQUIRE(pRequest->Token == request.Token);
    }

    {
        PartyAcceptInviteRequest request;
        request.InviterId = 123456;

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<PartyAcceptInviteRequest>(std::move(pMessage));
        REQUIRE(pRequest->InviterId == request.InviterId);
    }
}

TEST_CASE("Death state packets carry settled corpse positions", "[encoding.death_state]")
{
    RequestDeathStateChange request;
    request.Id = 42;
    request.OwnershipEpoch = 3;
    request.IsDead = true;
    request.IsSettledPosition = true;

    Buffer clientBuffer(1000);
    Buffer::Writer clientWriter(&clientBuffer);
    request.Serialize(clientWriter);

    Buffer::Reader clientReader(&clientBuffer);
    const ClientMessageFactory clientFactory;
    auto decodedRequest = CastUnique<RequestDeathStateChange>(clientFactory.Extract(clientReader));
    REQUIRE(decodedRequest);
    REQUIRE(*decodedRequest == request);

    NotifyDeathStateChange notification;
    notification.Id = 42;
    notification.OwnershipEpoch = 3;
    notification.IsDead = true;
    notification.IsSettledPosition = true;
    notification.Position.x = -1234.f;
    notification.Position.y = 5678.f;
    notification.Position.z = 901.f;

    Buffer serverBuffer(1000);
    Buffer::Writer serverWriter(&serverBuffer);
    notification.Serialize(serverWriter);

    Buffer::Reader serverReader(&serverBuffer);
    const ServerMessageFactory serverFactory;
    auto decodedNotification = CastUnique<NotifyDeathStateChange>(serverFactory.Extract(serverReader));
    REQUIRE(decodedNotification);
    REQUIRE(*decodedNotification == notification);
}

TEST_CASE("AssignObjectsResponse preserves provisional object state", "[encoding.object_authority]")
{
    AssignObjectsResponse sent;
    ObjectData object{};
    object.ServerId = 42;
    object.Id = GameId{1, 0x200};
    object.IsStateUntrusted = true;
    sent.Objects.push_back(object);

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto message = factory.Extract(reader);

    REQUIRE(message);
    auto received = CastUnique<AssignObjectsResponse>(std::move(message));
    REQUIRE(received);
    REQUIRE(received->Objects.size() == 1);
    REQUIRE(received->Objects.front().IsStateUntrusted);
    REQUIRE(received->Objects.front() == object);
}

TEST_CASE("AssignObjectsResponse carries harvest state", "[encoding.object_authority][harvest]")
{
    AssignObjectsResponse sent;
    ObjectData object{};
    object.ServerId = 7;
    object.Id = GameId{1, 0x300};
    object.IsStateUntrusted = true;
    object.IsHarvestable = true;
    object.IsHarvested = true;
    object.IsHarvestItem = true;
    sent.Objects.push_back(object);

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto received = CastUnique<AssignObjectsResponse>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE(received->Objects.size() == 1);
    REQUIRE(received->Objects.front().IsHarvestable);
    REQUIRE(received->Objects.front().IsHarvested);
    REQUIRE(received->Objects.front().IsHarvestItem);
    REQUIRE(received->Objects.front() == object);
}

TEST_CASE("AssignObjectsResponse carries furniture discovery state", "[encoding.object_authority][furniture]")
{
    AssignObjectsResponse sent;
    ObjectData object{};
    object.ServerId = 13;
    object.Id = GameId{1, 0x360};
    object.IsFurniture = true;
    sent.Objects.push_back(object);

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto received = CastUnique<AssignObjectsResponse>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE(received->Objects.size() == 1);
    REQUIRE(received->Objects.front().IsFurniture);
    REQUIRE(received->Objects.front() == object);
}

TEST_CASE("AssignObjectsResponse carries open loot state", "[encoding.object_authority][world_loot]")
{
    AssignObjectsResponse sent;
    ObjectData object{};
    object.ServerId = 11;
    object.Id = GameId{1, 0x350};
    object.IsStateUntrusted = true;
    object.IsOpenLoot = true;
    object.IsLootTaken = true;
    sent.Objects.push_back(object);

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto received = CastUnique<AssignObjectsResponse>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE(received->Objects.size() == 1);
    REQUIRE(received->Objects.front().IsOpenLoot);
    REQUIRE(received->Objects.front().IsLootTaken);
    REQUIRE(received->Objects.front() == object);
}

TEST_CASE("AssignObjectsResponse carries door state", "[encoding.object_authority][door]")
{
    AssignObjectsResponse sent;
    ObjectData object{};
    object.ServerId = 9;
    object.Id = GameId{1, 0x400};
    object.IsDoor = true;
    object.IsDoorStateKnown = true;
    object.IsDoorOpen = true;
    sent.Objects.push_back(object);

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto received = CastUnique<AssignObjectsResponse>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE(received->Objects.size() == 1);
    REQUIRE(received->Objects.front() == object);
}

TEST_CASE("NotifyObjectHarvested round-trips", "[encoding.object_authority][harvest]")
{
    NotifyObjectHarvested sent;
    sent.Id = GameId{2, 0x456};
    sent.IsHarvested = true;

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto received = CastUnique<NotifyObjectHarvested>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE(*received == sent);
}

TEST_CASE("TakeWorldItemRequest round-trips", "[encoding.object_authority][world_loot]")
{
    TakeWorldItemRequest sent;
    sent.Id = GameId{2, 0x456};
    sent.CellId = GameId{0, 0x1234};
    sent.ActivatorId = 0xABC;

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ClientMessageFactory factory;
    auto received = CastUnique<TakeWorldItemRequest>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE(*received == sent);
}

TEST_CASE("NotifyWorldItemTaken round-trips", "[encoding.object_authority][world_loot]")
{
    NotifyWorldItemTaken sent;
    sent.Id = GameId{2, 0x456};
    sent.IsTaken = false;

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto received = CastUnique<NotifyWorldItemTaken>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE(*received == sent);
    REQUIRE_FALSE(received->IsTaken);
}

TEST_CASE("Static structures", "[encoding.static]")
{
    GIVEN("GameId")
    {
        GameId sendObjects, recvObjects;
        sendObjects.ModId = 1456987;
        sendObjects.BaseId = 0x789654;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Vector3_NetQuantize")
    {
        Vector3_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 142.56f;
        sendObjects.y = 45687.7f;
        sendObjects.z = -142.56f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Vector2_NetQuantize")
    {
        Vector2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 1000.89f;
        sendObjects.y = -485632.75f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Rotator2_NetQuantize")
    {
        Rotator2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 1.89f;
        sendObjects.y = TiltedPhoques::Pi * 2.0f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Rotator2_NetQuantize needing wrap")
    {
        // This test is a bit dangerous as floating errors can lead to sendObjects != recvObjects but the difference is minuscule so we don't care abut such cases
        Rotator2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = -1.87f;
        sendObjects.y = static_cast<float>(TiltedPhoques::Pi) * 18.0f + 3.6f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }
}

TEST_CASE("Differential structures", "[encoding.differential]")
{
    GIVEN("Full ActionEvent")
    {
        ActionEvent sendAction, recvAction;

        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = GameId{0x12, 963741};
        sendAction.Type = 4;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.EventName = "Plot twist !";

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }
    }

    GIVEN("A single cached event name")
    {
        ActionEvent sendAction, recvAction;

        TP_UNUSED(StringCache::Get().Add("test"))

        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = GameId{0x12, 963741};
        sendAction.Type = 4;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.EventName = "Plot twist !";

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }
    }

    GIVEN("Full Mods")
    {
        Mods sendMods, recvMods;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);

        sendMods.ModList.push_back({"Hello", 42});
        sendMods.ModList.push_back({"Hi", 14});
        sendMods.ModList.push_back({"Test", 8});
        sendMods.ModList.push_back({"Toast", 49});

        sendMods.Serialize(writer);

        Buffer::Reader reader(&buff);
        recvMods.Deserialize(reader);

        REQUIRE(sendMods == recvMods);
    }

    GIVEN("AnimationVariables")
    {
        AnimationVariables vars, recvVars;
 
        vars.Booleans.resize(76);
        String testString("\xDE\xAD\xBE\xEF"
                          "\xDE\xAD\xBE\xEF\x76\xB");
        vars.String_to_VectorBool(testString, vars.Booleans);

        vars.Floats.push_back(1.f);
        vars.Floats.push_back(7.f);
        vars.Floats.push_back(12.f);
        vars.Floats.push_back(0.f);
        vars.Floats.push_back(145.f);
        vars.Floats.push_back(100.f);
        vars.Floats.push_back(-1.f);

        vars.Integers.push_back(0);
        vars.Integers.push_back(12000);
        vars.Integers.push_back(06);
        vars.Integers.push_back(7778);
        vars.Integers.push_back(41104539);

        Buffer buff(1000);
        {
            Buffer::Writer writer(&buff);

            vars.GenerateDiff(recvVars, writer);

            Buffer::Reader reader(&buff);
            recvVars.ApplyDiff(reader);

            REQUIRE(vars.Booleans == recvVars.Booleans);
            REQUIRE(vars.Floats == recvVars.Floats);
            REQUIRE(vars.Integers == recvVars.Integers);
        }

        vars.Booleans.resize(33);
        vars.Booleans[16] = false;
        vars.Booleans[17] = false;
        vars.Booleans[18] = false;
        vars.Booleans[19] = false;
        vars.Floats[3] = 42.f;
        vars.Integers[0] = 18;
        vars.Integers[3] = 0;

        {
            Buffer::Writer writer(&buff);

            vars.GenerateDiff(recvVars, writer);

            Buffer::Reader reader(&buff);
            recvVars.ApplyDiff(reader);

            REQUIRE(vars.Booleans == recvVars.Booleans);
            REQUIRE(vars.Floats == recvVars.Floats);
            REQUIRE(vars.Integers == recvVars.Integers);
        }
    }
}

TEST_CASE("Packets", "[encoding.packets]")
{
    SECTION("AuthenticationRequest")
    {
        Buffer buff(1000);

        AuthenticationRequest sendMessage, recvMessage;
        sendMessage.Token = "TesSt";
        sendMessage.Username = "Dragonborn";
        sendMessage.RaceFormId = 0x00013746;
        sendMessage.Sex = 1;
        sendMessage.Position = glm::vec3{1.f, 2.f, 3.f};
        sendMessage.WorldSpaceFormId = 0x0000003C;
        sendMessage.CellFormId = 0x0000003D;
        sendMessage.WorldSpaceId = GameId(1, 0x0000003C);
        sendMessage.CellId = GameId(1, 0x0000003D);
        sendMessage.Level = 42;
        sendMessage.UserMods.ModList.push_back({"Hello", 42});
        sendMessage.UserMods.ModList.push_back({"Hi", 14});
        sendMessage.UserMods.ModList.push_back({"Test", 8});
        sendMessage.UserMods.ModList.push_back({"Toast", 49});

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("AuthenticationResponse")
    {
        Buffer buff(1000);

        AuthenticationResponse sendMessage, recvMessage;
        sendMessage.Type = AuthenticationResponse::ResponseType::kAccepted;
        sendMessage.UserMods.ModList.push_back({"Hello", 42});
        sendMessage.UserMods.ModList.push_back({"Hi", 14});
        sendMessage.UserMods.ModList.push_back({"Test", 8});
        sendMessage.UserMods.ModList.push_back({"Toast", 49});

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("AssignCharacterRequest")
    {
        Buffer buff(1000);

        ActionEvent sendAction;
        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = GameId{0x12, 963741};
        sendAction.Type = 4;

        AssignCharacterRequest sendMessage, recvMessage;
        sendMessage.Cookie = 14523698;
        sendMessage.AppearanceBuffer = "toto";
        sendMessage.CellId.BaseId = 45;
        sendMessage.FormId.ModId = 48;
        sendMessage.ReferenceId.BaseId = 456799;
        sendMessage.ReferenceId.ModId = 4079;
        sendMessage.LatestAction = sendAction;
        sendMessage.Position.x = -452.4f;
        sendMessage.Position.y = 452.4f;
        sendMessage.Position.z = 125452.4f;
        sendMessage.Rotation.x = -1.87f;
        sendMessage.Rotation.y = 45.35f;

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    GIVEN("ClientReferencesMoveRequest")
    {
        ClientReferencesMoveRequest sendMessage, recvMessage;
        auto& update = sendMessage.Updates[1];
        update.OwnershipEpoch = 17;
        auto& move = update.UpdatedMovement;

        AnimationVariables vars;
        vars.Booleans.resize(76);
        String testString("\xDE\xAD\xBE\xEF\x76\xB");
        vars.String_to_VectorBool(testString, vars.Booleans);

        vars.Floats.push_back(1.f);
        vars.Floats.push_back(7.f);
        vars.Floats.push_back(12.f);
        vars.Floats.push_back(0.f);
        vars.Floats.push_back(145.f);
        vars.Floats.push_back(100.f);
        vars.Floats.push_back(-1.f);

        vars.Integers.push_back(0);
        vars.Integers.push_back(12000);
        vars.Integers.push_back(06);
        vars.Integers.push_back(7778);
        vars.Integers.push_back(41104539);

        move.Variables = vars;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(recvMessage.Updates[1].OwnershipEpoch == sendMessage.Updates[1].OwnershipEpoch);
        REQUIRE(recvMessage.Updates[1].UpdatedMovement == sendMessage.Updates[1].UpdatedMovement);
    }
}

TEST_CASE("StringCache", "[encoding.string_cache]")
{
    SECTION("Messages")
    {
        StringCacheUpdate update;
        update.Values.push_back("Hello");
        update.Values.push_back("Bye");

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        update.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        StringCacheUpdate recvUpdate;
        recvUpdate.DeserializeRaw(reader);

        REQUIRE(update == recvUpdate);
    }
}

TEST_CASE("AssignObjectsResponse carries activator state", "[encoding.object_authority][activator]")
{
    AssignObjectsResponse sent;
    ObjectData object{};
    object.ServerId = 11;
    object.Id = GameId{1, 0x500};
    object.IsActivator = true;
    object.ActivationCount = 300;
    sent.Objects.push_back(object);

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto received = CastUnique<AssignObjectsResponse>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE(received->Objects.size() == 1);
    REQUIRE(received->Objects.front().ActivationCount == 300);
    REQUIRE(received->Objects.front() == object);
}

TEST_CASE("Furniture denial carries the authoritative position to the owning client", "[encoding.furniture]")
{
    NotifyFurnitureUseDenied sent;
    sent.ActorId = 45;
    sent.OwnershipEpoch = 3;
    sent.AuthoritativeMovement.WorldSpaceId = GameId{2, 0x100};
    sent.AuthoritativeMovement.CellId = GameId{1, 0x200};
    sent.AuthoritativeMovement.Position.x = 120.f;
    sent.AuthoritativeMovement.Position.y = -42.f;
    sent.AuthoritativeMovement.Position.z = 96.f;
    sent.AuthoritativeMovement.Rotation.x = 15.f;
    sent.AuthoritativeMovement.Rotation.y = -35.f;

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto received = CastUnique<NotifyFurnitureUseDenied>(factory.Extract(reader));

    REQUIRE(received);
    REQUIRE(received->ActorId == sent.ActorId);
    REQUIRE(received->OwnershipEpoch == sent.OwnershipEpoch);
    REQUIRE(received->AuthoritativeMovement == sent.AuthoritativeMovement);
}

TEST_CASE("RequestContainerTransfer round-trips", "[encoding.container_transfer]")
{
    RequestContainerTransfer sent;
    sent.RequestId = 77;
    sent.ContainerId = 1234;
    sent.TargetKind = 1;
    sent.Direction = 1;
    sent.ExpectedContainerCount = 5;
    sent.Item.BaseId = GameId{0, 0x13989};
    sent.Item.Count = 2;

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ClientMessageFactory factory;
    auto received = CastUnique<RequestContainerTransfer>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE(*received == sent);
}

TEST_CASE("NotifyContainerTransferResult round-trips", "[encoding.container_transfer]")
{
    NotifyContainerTransferResult sent;
    sent.RequestId = 77;
    sent.Result = 1;

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto received = CastUnique<NotifyContainerTransferResult>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE(*received == sent);
}

TEST_CASE("NotifyCorpseContents round-trips", "[encoding.container_transfer]")
{
    NotifyCorpseContents sent;
    sent.ServerId = 0x1C;
    Inventory::Entry axe{};
    axe.BaseId = GameId{0, 0x1CB64};
    axe.Count = 1;
    sent.Contents.AddOrRemoveEntry(axe);
    Inventory::Entry gold{};
    gold.BaseId = GameId{0, 0xF};
    gold.Count = 23;
    sent.Contents.AddOrRemoveEntry(gold);

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto received = CastUnique<NotifyCorpseContents>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE(*received == sent);
}

// Lists sized like real cells: a 1-byte count once wrapped 308 objects in Bleak Falls Barrow to 52.
namespace
{
constexpr uint32_t kRealisticListSize = 308;

template <class T, class TFactory> auto RoundTrip(const T& acSent, const TFactory& acFactory, const size_t aBufferSize = 1 << 17)
{
    Buffer buffer(aBufferSize);
    Buffer::Writer writer(&buffer);
    acSent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    return CastUnique<T>(acFactory.Extract(reader));
}
} // namespace

TEST_CASE("AssignObjectsRequest keeps every object of a large cell", "[encoding.list_counts]")
{
    AssignObjectsRequest sent;
    for (uint32_t i = 0; i < kRealisticListSize; ++i)
    {
        ObjectData object{};
        object.Id = GameId{0, 0xAA000 + i};
        object.CellId = GameId{0, 0x371DE};
        object.IsActivator = (i % 2) == 0;
        sent.Objects.push_back(object);
    }

    const auto received = RoundTrip(sent, ClientMessageFactory{});
    REQUIRE(received);
    REQUIRE(received->Objects.size() == kRealisticListSize);
    REQUIRE(*received == sent);
}

TEST_CASE("AssignObjectsResponse keeps every object of a large cell", "[encoding.list_counts]")
{
    AssignObjectsResponse sent;
    for (uint32_t i = 0; i < kRealisticListSize; ++i)
    {
        ObjectData object{};
        object.ServerId = i;
        object.Id = GameId{0, 0xAA000 + i};
        sent.Objects.push_back(object);
    }

    const auto received = RoundTrip(sent, ServerMessageFactory{});
    REQUIRE(received);
    REQUIRE(received->Objects.size() == kRealisticListSize);
    REQUIRE(*received == sent);
}

TEST_CASE("Party and grid lists survive more than 255 entries", "[encoding.list_counts]")
{
    NotifyPartyInfo party;
    NotifyPartyJoined joined;
    ShiftGridCellRequest grid;
    for (uint32_t i = 0; i < kRealisticListSize; ++i)
    {
        party.PlayerIds.push_back(i);
        joined.PlayerIds.push_back(i);
        grid.Cells.push_back(GameId{0, 0x1000 + i});
    }

    const auto receivedParty = RoundTrip(party, ServerMessageFactory{});
    REQUIRE(receivedParty);
    REQUIRE(receivedParty->PlayerIds.size() == kRealisticListSize);

    const auto receivedJoined = RoundTrip(joined, ServerMessageFactory{});
    REQUIRE(receivedJoined);
    REQUIRE(receivedJoined->PlayerIds.size() == kRealisticListSize);

    const auto receivedGrid = RoundTrip(grid, ClientMessageFactory{});
    REQUIRE(receivedGrid);
    REQUIRE(*receivedGrid == grid);
}

TEST_CASE("ActionReplayChain keeps more than 255 actions", "[encoding.list_counts]")
{
    ActionReplayChain sent;
    for (uint32_t i = 0; i < kRealisticListSize; ++i)
    {
        ActionEvent action{};
        action.Tick = i;
        action.ActionId = 0x1000 + i;
        sent.Actions.push_back(action);
    }

    Buffer buffer(1 << 17);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    ActionReplayChain received;
    Buffer::Reader reader(&buffer);
    received.Deserialize(reader);

    REQUIRE(received.Actions.size() == kRealisticListSize);
    REQUIRE(received == sent);
}

TEST_CASE("A malformed object count is refused instead of allocated", "[encoding.list_counts]")
{
    Buffer buffer(64);
    Buffer::Writer writer(&buffer);
    Serialization::WriteVarInt(writer, 1'000'000'000);

    AssignObjectsRequest received;
    Buffer::Reader reader(&buffer);
    received.DeserializeRaw(reader);

    REQUIRE(received.Objects.empty());
    // The server drops such a message with a [Drop] line instead of handling an empty list.
    REQUIRE(received.OverLimitCount == 1'000'000'000);
}

TEST_CASE("ObjectStateReport round-trips", "[encoding.object_authority][desync]")
{
    ObjectStateReport sent;
    ObjectStateDigest door;
    door.Id = GameId{0, 0x99535};
    door.CellId = GameId{0, 0x1234};
    door.StateFlags = ObjectStateDigest::kLocked | ObjectStateDigest::kDoorOpen;
    door.LockLevel = 50;
    ObjectStateDigest chest;
    chest.Id = GameId{2, 0x456};
    chest.CellId = GameId{0, 0x1234};
    chest.StateFlags = ObjectStateDigest::kHasInventory;
    chest.Items = {{GameId{0, 0xF}, 7}, {GameId{0, 0xA44AE}, 1}};
    sent.Objects = {door, chest};

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    const ClientMessageFactory factory;
    auto received = CastUnique<ObjectStateReport>(factory.Extract(reader));
    REQUIRE(received);
    REQUIRE_FALSE(received->IsMalformed);
    REQUIRE(*received == sent);
}

TEST_CASE("ObjectStateDigest canonicalizes inventories", "[desync]")
{
    Inventory inventory;
    Inventory::Entry gold;
    gold.BaseId = GameId{0, 0xF};
    gold.Count = 5;
    Inventory::Entry moreGold = gold;
    moreGold.Count = 2;
    Inventory::Entry removed;
    removed.BaseId = GameId{0, 0x10};
    removed.Count = 0;
    Inventory::Entry scroll;
    scroll.BaseId = GameId{0, 0x9};
    scroll.Count = 1;
    inventory.Entries = {gold, removed, scroll, moreGold};

    const auto items = ObjectStateDigest::Canonicalize(inventory);
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].BaseId == GameId{0, 0x9});
    REQUIRE(items[1].BaseId == GameId{0, 0xF});
    REQUIRE(items[1].Count == 7);
}
