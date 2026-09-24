#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <catch2/catch.hpp>

#include <Messages/ClientMessageFactory.h>
#include <Messages/CharacterReadyRequest.h>
#include <Messages/CreateCharacterRequest.h>
#include <Messages/NotifyCharacterLoadSnapshot.h>
#include <Messages/NotifyCharacterEnteredWorld.h>
#include <Messages/NotifyCharacterAssignmentRejected.h>
#include <Messages/NotifyCharacterList.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyCharacterReadyResult.h>
#include <Messages/NotifyCharacterSelectionResult.h>
#include <Messages/NotifyCharacterCreateResult.h>
#include <Messages/NotifyCharacterSlots.h>
#include <Messages/UpdateCharacterAppearanceRequest.h>
#include <Messages/RequestCharacterList.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/AddTargetRequest.h>
#include <Messages/RemoveSpellRequest.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyRemoveSpell.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/NotifyProjectileLaunch.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/NotifySpellCast.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/RequestRespawn.h>
#include <Messages/SelectCharacterRequest.h>
#include <Messages/SpellCastRequest.h>
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

        CreateCharacterRequest createRequest{};
        createRequest.SlotIndex = 2;
        createRequest.Name = "Ava O'Kynareth";
        TiltedPhoques::Buffer createBuffer(256);
        TiltedPhoques::Buffer::Writer createWriter(&createBuffer);
        createRequest.Serialize(createWriter);

        TiltedPhoques::Buffer::Reader createReader(&createBuffer);
        auto createMessage = clientFactory.Extract(createReader);
        REQUIRE(createMessage);
        auto parsedCreateRequest = TiltedPhoques::CastUnique<CreateCharacterRequest>(std::move(createMessage));
        REQUIRE(*parsedCreateRequest == createRequest);

        UpdateCharacterAppearanceRequest appearanceRequest{};
        appearanceRequest.Race = GameId(0x01020304, 0x05060708);
        appearanceRequest.Sex = 1;
        TiltedPhoques::Buffer appearanceBuffer(256);
        TiltedPhoques::Buffer::Writer appearanceWriter(&appearanceBuffer);
        appearanceRequest.Serialize(appearanceWriter);

        TiltedPhoques::Buffer::Reader appearanceReader(&appearanceBuffer);
        auto appearanceMessage = clientFactory.Extract(appearanceReader);
        REQUIRE(appearanceMessage);
        auto parsedAppearanceRequest = TiltedPhoques::CastUnique<UpdateCharacterAppearanceRequest>(std::move(appearanceMessage));
        REQUIRE(*parsedAppearanceRequest == appearanceRequest);

        CharacterReadyRequest readyRequest{};
        readyRequest.CharacterId = std::numeric_limits<std::uint64_t>::max();
        readyRequest.PositionX = -2048.5f;
        readyRequest.PositionY = 1024.25f;
        readyRequest.PositionZ = 128.f;
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

        DrawWeaponRequest drawWeaponRequest{};
        drawWeaponRequest.Id = 0x2345;
        drawWeaponRequest.OwnershipEpoch = 67;
        drawWeaponRequest.IsWeaponDrawn = true;
        TiltedPhoques::Buffer drawWeaponBuffer(256);
        TiltedPhoques::Buffer::Writer drawWeaponWriter(&drawWeaponBuffer);
        drawWeaponRequest.Serialize(drawWeaponWriter);

        TiltedPhoques::Buffer::Reader drawWeaponReader(&drawWeaponBuffer);
        auto drawWeaponMessage = clientFactory.Extract(drawWeaponReader);
        REQUIRE(drawWeaponMessage);
        auto parsedDrawWeaponRequest = TiltedPhoques::CastUnique<DrawWeaponRequest>(std::move(drawWeaponMessage));
        REQUIRE(*parsedDrawWeaponRequest == drawWeaponRequest);

        ProjectileLaunchRequest projectileRequest{};
        projectileRequest.ShooterID = 0x1234;
        projectileRequest.OriginX = 1.5f;
        projectileRequest.Power = 2.5f;
        projectileRequest.Scale = 0.75f;
        projectileRequest.OwnershipEpoch = 19;
        TiltedPhoques::Buffer projectileBuffer(512);
        TiltedPhoques::Buffer::Writer projectileWriter(&projectileBuffer);
        projectileRequest.Serialize(projectileWriter);

        TiltedPhoques::Buffer::Reader projectileReader(&projectileBuffer);
        auto projectileMessage = clientFactory.Extract(projectileReader);
        REQUIRE(projectileMessage);
        auto parsedProjectileRequest = TiltedPhoques::CastUnique<ProjectileLaunchRequest>(std::move(projectileMessage));
        REQUIRE(*parsedProjectileRequest == projectileRequest);

        NewPackageRequest packageRequest{};
        packageRequest.ActorId = 0x2345;
        packageRequest.PackageId = GameId(0x01, 0x00001234);
        packageRequest.OwnershipEpoch = 31;
        TiltedPhoques::Buffer packageBuffer(256);
        TiltedPhoques::Buffer::Writer packageWriter(&packageBuffer);
        packageRequest.Serialize(packageWriter);

        TiltedPhoques::Buffer::Reader packageReader(&packageBuffer);
        auto packageMessage = clientFactory.Extract(packageReader);
        REQUIRE(packageMessage);
        auto parsedPackageRequest = TiltedPhoques::CastUnique<NewPackageRequest>(std::move(packageMessage));
        REQUIRE(*parsedPackageRequest == packageRequest);

        SpellCastRequest spellRequest{};
        spellRequest.CasterId = 0x3456;
        spellRequest.DesiredTarget = 0x5678;
        spellRequest.OwnershipEpoch = 37;
        TiltedPhoques::Buffer spellBuffer(256);
        TiltedPhoques::Buffer::Writer spellWriter(&spellBuffer);
        spellRequest.Serialize(spellWriter);

        TiltedPhoques::Buffer::Reader spellReader(&spellBuffer);
        auto spellMessage = clientFactory.Extract(spellReader);
        REQUIRE(spellMessage);
        auto parsedSpellRequest = TiltedPhoques::CastUnique<SpellCastRequest>(std::move(spellMessage));
        REQUIRE(*parsedSpellRequest == spellRequest);

        InterruptCastRequest interruptRequest{};
        interruptRequest.CasterId = 0x4567;
        interruptRequest.OwnershipEpoch = 41;
        TiltedPhoques::Buffer interruptBuffer(256);
        TiltedPhoques::Buffer::Writer interruptWriter(&interruptBuffer);
        interruptRequest.Serialize(interruptWriter);

        TiltedPhoques::Buffer::Reader interruptReader(&interruptBuffer);
        auto interruptMessage = clientFactory.Extract(interruptReader);
        REQUIRE(interruptMessage);
        auto parsedInterruptRequest = TiltedPhoques::CastUnique<InterruptCastRequest>(std::move(interruptMessage));
        REQUIRE(*parsedInterruptRequest == interruptRequest);

        AddTargetRequest addTargetRequest{};
        addTargetRequest.TargetId = 0x6789;
        addTargetRequest.CasterId = 0x789A;
        addTargetRequest.SpellId = GameId(0x01, 0x0000002A);
        addTargetRequest.EffectId = GameId(0x02, 0x00000035);
        addTargetRequest.Magnitude = 12.5f;
        addTargetRequest.TargetOwnershipEpoch = 43;
        addTargetRequest.CasterOwnershipEpoch = 47;
        TiltedPhoques::Buffer addTargetBuffer(256);
        TiltedPhoques::Buffer::Writer addTargetWriter(&addTargetBuffer);
        addTargetRequest.Serialize(addTargetWriter);

        TiltedPhoques::Buffer::Reader addTargetReader(&addTargetBuffer);
        auto addTargetMessage = clientFactory.Extract(addTargetReader);
        REQUIRE(addTargetMessage);
        auto parsedAddTargetRequest = TiltedPhoques::CastUnique<AddTargetRequest>(std::move(addTargetMessage));
        REQUIRE(*parsedAddTargetRequest == addTargetRequest);

        RemoveSpellRequest removeSpellRequest{};
        removeSpellRequest.TargetId = 0x89AB;
        removeSpellRequest.SpellId = GameId(0x03, 0x00000046);
        removeSpellRequest.OwnershipEpoch = 53;
        TiltedPhoques::Buffer removeSpellBuffer(256);
        TiltedPhoques::Buffer::Writer removeSpellWriter(&removeSpellBuffer);
        removeSpellRequest.Serialize(removeSpellWriter);

        TiltedPhoques::Buffer::Reader removeSpellReader(&removeSpellBuffer);
        auto removeSpellMessage = clientFactory.Extract(removeSpellReader);
        REQUIRE(removeSpellMessage);
        auto parsedRemoveSpellRequest = TiltedPhoques::CastUnique<RemoveSpellRequest>(std::move(removeSpellMessage));
        REQUIRE(*parsedRemoveSpellRequest == removeSpellRequest);

        RequestRespawn respawnRequest{};
        respawnRequest.ActorId = 0x5678;
        respawnRequest.OwnershipEpoch = 43;
        TiltedPhoques::Buffer respawnBuffer(256);
        TiltedPhoques::Buffer::Writer respawnWriter(&respawnBuffer);
        respawnRequest.Serialize(respawnWriter);

        TiltedPhoques::Buffer::Reader respawnReader(&respawnBuffer);
        auto respawnMessage = clientFactory.Extract(respawnReader);
        REQUIRE(respawnMessage);
        auto parsedRespawnRequest = TiltedPhoques::CastUnique<RequestRespawn>(std::move(respawnMessage));
        REQUIRE(*parsedRespawnRequest == respawnRequest);
    }

    SECTION("server list and selection result")
    {
        NotifyCharacterList list{};
        list.Characters = {
            CharacterSummary{42, "Aela", GameId(0x01, 0x00013746), 0, 18, 0},
            CharacterSummary{std::numeric_limits<std::uint64_t>::max(), "O'Reilly", GameId(0x02, 0x0000003c), 1, 27, 2}};

        TiltedPhoques::Buffer listBuffer(1024);
        TiltedPhoques::Buffer::Writer listWriter(&listBuffer);
        list.Serialize(listWriter);

        TiltedPhoques::Buffer::Reader listReader(&listBuffer);
        const ServerMessageFactory serverFactory;
        auto listMessage = serverFactory.Extract(listReader);
        REQUIRE(listMessage);
        auto parsedList = TiltedPhoques::CastUnique<NotifyCharacterList>(std::move(listMessage));
        REQUIRE(*parsedList == list);

        NotifyCharacterSlots slots{};
        slots.Total = 3;
        slots.Unlocked = 1;
        TiltedPhoques::Buffer slotsBuffer(256);
        TiltedPhoques::Buffer::Writer slotsWriter(&slotsBuffer);
        slots.Serialize(slotsWriter);

        TiltedPhoques::Buffer::Reader slotsReader(&slotsBuffer);
        auto slotsMessage = serverFactory.Extract(slotsReader);
        REQUIRE(slotsMessage);
        auto parsedSlots = TiltedPhoques::CastUnique<NotifyCharacterSlots>(std::move(slotsMessage));
        REQUIRE(*parsedSlots == slots);

        NotifyCharacterCreateResult createResult{};
        createResult.Status = CharacterCreateStatus::kSuccess;
        createResult.CharacterId = std::numeric_limits<std::uint64_t>::max();
        TiltedPhoques::Buffer createResultBuffer(256);
        TiltedPhoques::Buffer::Writer createResultWriter(&createResultBuffer);
        createResult.Serialize(createResultWriter);

        TiltedPhoques::Buffer::Reader createResultReader(&createResultBuffer);
        auto createResultMessage = serverFactory.Extract(createResultReader);
        REQUIRE(createResultMessage);
        auto parsedCreateResult = TiltedPhoques::CastUnique<NotifyCharacterCreateResult>(std::move(createResultMessage));
        REQUIRE(*parsedCreateResult == createResult);

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
        snapshotMessage.Snapshot.NeedsRaceMenu = true;

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

        NotifyProjectileLaunch projectileNotification{};
        projectileNotification.ShooterID = 0x9ABC;
        projectileNotification.OriginZ = -4.5f;
        projectileNotification.Power = 3.5f;
        projectileNotification.Scale = 1.25f;
        projectileNotification.OwnershipEpoch = 29;
        TiltedPhoques::Buffer projectileNotificationBuffer(512);
        TiltedPhoques::Buffer::Writer projectileNotificationWriter(&projectileNotificationBuffer);
        projectileNotification.Serialize(projectileNotificationWriter);

        TiltedPhoques::Buffer::Reader projectileNotificationReader(&projectileNotificationBuffer);
        auto projectileNotificationMessage = serverFactory.Extract(projectileNotificationReader);
        REQUIRE(projectileNotificationMessage);
        auto parsedProjectileNotification = TiltedPhoques::CastUnique<NotifyProjectileLaunch>(std::move(projectileNotificationMessage));
        REQUIRE(*parsedProjectileNotification == projectileNotification);

        NotifyNewPackage packageNotification{};
        packageNotification.ActorId = 0x6789;
        packageNotification.OwnershipEpoch = 47;
        TiltedPhoques::Buffer packageNotificationBuffer(256);
        TiltedPhoques::Buffer::Writer packageNotificationWriter(&packageNotificationBuffer);
        packageNotification.Serialize(packageNotificationWriter);

        TiltedPhoques::Buffer::Reader packageNotificationReader(&packageNotificationBuffer);
        auto packageNotificationMessage = serverFactory.Extract(packageNotificationReader);
        REQUIRE(packageNotificationMessage);
        auto parsedPackageNotification = TiltedPhoques::CastUnique<NotifyNewPackage>(std::move(packageNotificationMessage));
        REQUIRE(*parsedPackageNotification == packageNotification);

        NotifySpellCast spellNotification{};
        spellNotification.CasterId = 0x789A;
        spellNotification.OwnershipEpoch = 53;
        TiltedPhoques::Buffer spellNotificationBuffer(256);
        TiltedPhoques::Buffer::Writer spellNotificationWriter(&spellNotificationBuffer);
        spellNotification.Serialize(spellNotificationWriter);

        TiltedPhoques::Buffer::Reader spellNotificationReader(&spellNotificationBuffer);
        auto spellNotificationMessage = serverFactory.Extract(spellNotificationReader);
        REQUIRE(spellNotificationMessage);
        auto parsedSpellNotification = TiltedPhoques::CastUnique<NotifySpellCast>(std::move(spellNotificationMessage));
        REQUIRE(*parsedSpellNotification == spellNotification);

        NotifyInterruptCast interruptNotification{};
        interruptNotification.CasterId = 0x89AB;
        interruptNotification.OwnershipEpoch = 59;
        TiltedPhoques::Buffer interruptNotificationBuffer(256);
        TiltedPhoques::Buffer::Writer interruptNotificationWriter(&interruptNotificationBuffer);
        interruptNotification.Serialize(interruptNotificationWriter);

        TiltedPhoques::Buffer::Reader interruptNotificationReader(&interruptNotificationBuffer);
        auto interruptNotificationMessage = serverFactory.Extract(interruptNotificationReader);
        REQUIRE(interruptNotificationMessage);
        auto parsedInterruptNotification = TiltedPhoques::CastUnique<NotifyInterruptCast>(std::move(interruptNotificationMessage));
        REQUIRE(*parsedInterruptNotification == interruptNotification);

        NotifyAddTarget addTargetNotification{};
        addTargetNotification.TargetId = 0x9ABC;
        addTargetNotification.CasterId = 0xABCD;
        addTargetNotification.SpellId = GameId(0x01, 0x00000057);
        addTargetNotification.EffectId = GameId(0x02, 0x00000068);
        addTargetNotification.Magnitude = -2.5f;
        addTargetNotification.TargetOwnershipEpoch = 67;
        addTargetNotification.CasterOwnershipEpoch = 71;
        TiltedPhoques::Buffer addTargetNotificationBuffer(256);
        TiltedPhoques::Buffer::Writer addTargetNotificationWriter(&addTargetNotificationBuffer);
        addTargetNotification.Serialize(addTargetNotificationWriter);

        TiltedPhoques::Buffer::Reader addTargetNotificationReader(&addTargetNotificationBuffer);
        auto addTargetNotificationMessage = serverFactory.Extract(addTargetNotificationReader);
        REQUIRE(addTargetNotificationMessage);
        auto parsedAddTargetNotification = TiltedPhoques::CastUnique<NotifyAddTarget>(std::move(addTargetNotificationMessage));
        REQUIRE(*parsedAddTargetNotification == addTargetNotification);

        NotifyRemoveSpell removeSpellNotification{};
        removeSpellNotification.TargetId = 0xBCDE;
        removeSpellNotification.SpellId = GameId(0x03, 0x00000079);
        removeSpellNotification.OwnershipEpoch = 73;
        TiltedPhoques::Buffer removeSpellNotificationBuffer(256);
        TiltedPhoques::Buffer::Writer removeSpellNotificationWriter(&removeSpellNotificationBuffer);
        removeSpellNotification.Serialize(removeSpellNotificationWriter);

        TiltedPhoques::Buffer::Reader removeSpellNotificationReader(&removeSpellNotificationBuffer);
        auto removeSpellNotificationMessage = serverFactory.Extract(removeSpellNotificationReader);
        REQUIRE(removeSpellNotificationMessage);
        auto parsedRemoveSpellNotification = TiltedPhoques::CastUnique<NotifyRemoveSpell>(std::move(removeSpellNotificationMessage));
        REQUIRE(*parsedRemoveSpellNotification == removeSpellNotification);

        NotifyRespawn respawnNotification{};
        respawnNotification.ActorId = 0x9ABC;
        respawnNotification.OwnershipEpoch = 61;
        TiltedPhoques::Buffer respawnNotificationBuffer(256);
        TiltedPhoques::Buffer::Writer respawnNotificationWriter(&respawnNotificationBuffer);
        respawnNotification.Serialize(respawnNotificationWriter);

        TiltedPhoques::Buffer::Reader respawnNotificationReader(&respawnNotificationBuffer);
        auto respawnNotificationMessage = serverFactory.Extract(respawnNotificationReader);
        REQUIRE(respawnNotificationMessage);
        auto parsedRespawnNotification = TiltedPhoques::CastUnique<NotifyRespawn>(std::move(respawnNotificationMessage));
        REQUIRE(*parsedRespawnNotification == respawnNotification);

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
        REQUIRE(static_cast<unsigned>(kNotifyCharacterSlots) == static_cast<unsigned>(kNotifyCharacterAssignmentRejected) + 1);
        REQUIRE(static_cast<unsigned>(kNotifyCharacterCreateResult) == static_cast<unsigned>(kNotifyCharacterSlots) + 1);
        REQUIRE(static_cast<unsigned>(kNotifyObjectHarvested) == static_cast<unsigned>(kNotifyCharacterCreateResult) + 1);
        REQUIRE(static_cast<unsigned>(kServerOpcodeMax) == static_cast<unsigned>(kNotifyObjectHarvested) + 1);
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

TEST_CASE("Character outbound protocol policy lets in-world clients assign loaded actors", "[encoding.character_session]")
{
    // BeginWorldSync requests assignment for every loaded NPC once the session is in world;
    // blocking these left every NPC, corpse and creature client-local.
    REQUIRE(CanSendCharacterProtocolMessage(kAssignCharacterRequest, CharacterClientSessionPhase::kInWorld, false));
    REQUIRE(!CanSendCharacterProtocolMessage(kAssignCharacterRequest, CharacterClientSessionPhase::kApplyingCharacter, false));
    REQUIRE(!CanSendCharacterProtocolMessage(kAssignCharacterRequest, CharacterClientSessionPhase::kAwaitingClientReady, true));
}
