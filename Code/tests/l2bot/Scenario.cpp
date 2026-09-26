#include "Scenario.h"

#include <fixture/L2Fixture.h>

#include <Messages/AssignObjectsRequest.h>
#include <Messages/ActivateRequest.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/NotifyCharacterAssignmentRejected.h>
#include <Messages/NotifyActivate.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestDeathStateChange.h>
#include <Messages/NotifyContainerTransferResult.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestContainerTransfer.h>

#include <spdlog/spdlog.h>

#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <thread>

namespace
{
int Check(const bool aCondition, const char* apWhat)
{
    if (!aCondition)
        spdlog::error("CHECK failed: {}", apWhat);
    return aCondition ? 0 : 1;
}
} // namespace

bool Pump(Bots& aBots, const std::function<bool()>& aDone, const std::chrono::milliseconds aTimeout)
{
    const auto deadline = std::chrono::steady_clock::now() + aTimeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        for (auto& pBot : aBots)
        {
            pBot->Update();
            if (pBot->GetPhase() == Bot::Phase::kFailed)
                return false;
        }
        if (aDone())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return aDone();
}

// Step 2: a cell's worth of objects (more than 255, so every counter must be wider than a byte).
// Both bots report the same references and must get the same server ids; the server builds the
// chest's lock and contents and the placed leveled item from the plugin, not from the client.
int RunAssignObjects(Bots& aBots)
{
    constexpr uint32_t kObjectCount = 300;
    constexpr uint32_t kFillerBase = 0x001000;

    std::vector<std::optional<AssignObjectsResponse>> responses(aBots.size());
    for (size_t i = 0; i < aBots.size(); ++i)
    {
        aBots[i]->OnMessage = [&responses, i](const ServerMessage& acMessage)
        {
            if (acMessage.GetOpcode() == kAssignObjectsResponse && !responses[i])
                responses[i] = static_cast<const AssignObjectsResponse&>(acMessage);
        };
    }

    for (auto& pBot : aBots)
    {
        const uint32_t modId = pBot->GetFixtureModId();
        const GameId cell(modId, L2Fixture::kCell);

        AssignObjectsRequest request{};
        auto add = [&](const uint32_t aBaseId) -> ObjectData&
        {
            ObjectData& object = request.Objects.emplace_back();
            object.Id = GameId(modId, aBaseId);
            object.CellId = cell;
            return object;
        };
        add(L2Fixture::kChestRef).IsContainer = true;
        add(L2Fixture::kDoorRef).IsDoor = true;
        add(L2Fixture::kLeverRef).IsActivator = true;
        add(L2Fixture::kPlacedLootRef).IsOpenLoot = true;
        while (request.Objects.size() < kObjectCount)
            add(kFillerBase + static_cast<uint32_t>(request.Objects.size()));

        pBot->Send(request);
        // Sequential, so the second bot finds the objects the first one created.
        if (!Pump(aBots, [&] { return responses[&pBot - &aBots[0]].has_value(); }, std::chrono::seconds(10)))
        {
            spdlog::error("[{}] got no AssignObjectsResponse", pBot->GetName());
            return 1;
        }
    }

    for (auto& pBot : aBots)
        pBot->OnMessage = nullptr;

    int failures = 0;
    std::vector<std::map<uint32_t, ObjectData>> byBaseId(aBots.size());
    for (size_t i = 0; i < aBots.size(); ++i)
    {
        for (const auto& object : responses[i]->Objects)
            byBaseId[i][object.Id.BaseId] = object;
        failures += Check(byBaseId[i].size() == kObjectCount, "every reported object comes back");
    }

    std::set<uint32_t> serverIds;
    for (const auto& [baseId, object] : byBaseId[0])
    {
        serverIds.insert(object.ServerId);
        const auto other = byBaseId[1].find(baseId);
        if (other == byBaseId[1].end() || other->second.ServerId != object.ServerId)
        {
            spdlog::error("object {:X}: server id {} for {}, {} for {}", baseId, object.ServerId, aBots[0]->GetName(),
                other == byBaseId[1].end() ? 0u : other->second.ServerId, aBots[1]->GetName());
            ++failures;
        }
    }
    failures += Check(serverIds.size() == kObjectCount, "server ids are distinct");
    failures += Check(!serverIds.contains(0), "no object has server id zero");

    const auto& chest = byBaseId[1][L2Fixture::kChestRef];
    failures += Check(chest.IsContainer && !chest.IsStateUntrusted, "the chest has trusted server state");
    failures += Check(chest.CurrentLockData.IsLocked && chest.CurrentLockData.LockLevel == L2Fixture::kChestLockLevel,
        "the chest starts locked at the plugin's level");
    failures += Check(!chest.CurrentInventory.Entries.empty(), "the chest contents come from the plugin");

    const auto& loot = byBaseId[1][L2Fixture::kPlacedLootRef];
    failures += Check(loot.LeveledItemId.BaseId != 0, "the placed leveled item is resolved by the server");

    if (failures == 0)
        spdlog::info("PASS step 2: {} objects, same server ids for both bots, plugin state on the chest and the placed loot", kObjectCount);
    return failures;
}

bool EnterWorld(Bots& aBots, const std::string& acEndpoint)
{
    for (auto& pBot : aBots)
    {
        if (pBot->GetPhase() == Bot::Phase::kConnecting && !pBot->IsConnected() && !pBot->Connect(acEndpoint))
        {
            spdlog::error("[{}] could not start connecting to {}", pBot->GetName(), acEndpoint);
            return false;
        }
    }

    const bool inWorld = Pump(
        aBots,
        [&]
        {
            for (const auto& pBot : aBots)
                if (pBot->GetPhase() != Bot::Phase::kInWorld)
                    return false;
            return true;
        },
        std::chrono::seconds(30));

    for (const auto& pBot : aBots)
    {
        if (pBot->GetPhase() != Bot::Phase::kInWorld)
            spdlog::error("[{}] did not reach the world, stuck while {}", pBot->GetName(), ToString(pBot->GetPhase()));
    }
    return inWorld;
}

namespace
{
std::optional<AssignObjectsResponse> AssignFixtureObjects(Bots& aBots, Bot& aBot)
{
    std::optional<AssignObjectsResponse> response;
    aBot.OnMessage = [&](const ServerMessage& acMessage)
    {
        if (acMessage.GetOpcode() == kAssignObjectsResponse && !response)
            response = static_cast<const AssignObjectsResponse&>(acMessage);
    };

    const uint32_t modId = aBot.GetFixtureModId();
    AssignObjectsRequest request{};
    auto& door = request.Objects.emplace_back();
    door.Id = GameId(modId, L2Fixture::kDoorRef);
    door.CellId = GameId(modId, L2Fixture::kCell);
    door.IsDoor = true;
    auto& lever = request.Objects.emplace_back();
    lever.Id = GameId(modId, L2Fixture::kLeverRef);
    lever.CellId = GameId(modId, L2Fixture::kCell);
    lever.IsActivator = true;
    auto& chest = request.Objects.emplace_back();
    chest.Id = GameId(modId, L2Fixture::kChestRef);
    chest.CellId = GameId(modId, L2Fixture::kCell);
    chest.IsContainer = true;
    aBot.Send(request);

    static_cast<void>(Pump(aBots, [&] { return response.has_value(); }, std::chrono::seconds(10)));
    aBot.OnMessage = nullptr;
    return response;
}
} // namespace

int RunActivations(Bots& aBots, const std::string& acEndpoint, const std::string& acSecret)
{
    Bot& sender = *aBots[0];
    Bot& peer = *aBots[1];
    const uint32_t modId = sender.GetFixtureModId();

    std::set<uint32_t> relayed;
    peer.OnMessage = [&](const ServerMessage& acMessage)
    {
        if (acMessage.GetOpcode() == kNotifyActivate)
            relayed.insert(static_cast<const NotifyActivate&>(acMessage).Id.BaseId);
    };

    ActivateRequest door{};
    door.Id = GameId(modId, L2Fixture::kDoorRef);
    door.CellId = GameId(modId, L2Fixture::kCell);
    door.ActivatorId = sender.GetServerId();
    door.PreActivationOpenState = 3; // closed: this activation opens it
    sender.Send(door);

    ActivateRequest lever = door;
    lever.Id = GameId(modId, L2Fixture::kLeverRef);
    lever.PreActivationOpenState = 0;
    sender.Send(lever);

    const bool bothRelayed = Pump(
        aBots, [&] { return relayed.contains(L2Fixture::kDoorRef) && relayed.contains(L2Fixture::kLeverRef); }, std::chrono::seconds(5));
    peer.OnMessage = nullptr;

    int failures = 0;
    failures += Check(relayed.contains(L2Fixture::kDoorRef), "the peer is told the door was activated");
    failures += Check(relayed.contains(L2Fixture::kLeverRef), "the peer is told the lever was activated");
    if (!bothRelayed)
        return failures;

    // A late joiner learns the final state from the server, not from a replay of the activations.
    aBots.push_back(std::make_unique<Bot>(Bot::Config{"Charlie", 910000000000000003ull, acSecret, L2Fixture::kCell}));
    if (!EnterWorld(aBots, acEndpoint))
        return failures + 1;

    const auto response = AssignFixtureObjects(aBots, *aBots.back());
    failures += Check(response.has_value(), "the late joiner gets the door and the lever");
    if (response)
    {
        for (const auto& object : response->Objects)
        {
            if (object.Id.BaseId == L2Fixture::kDoorRef)
                failures += Check(object.IsDoorStateKnown && object.IsDoorOpen, "the late joiner sees the door open");
            else if (object.Id.BaseId == L2Fixture::kLeverRef)
                failures += Check(object.ActivationCount == 1, "the late joiner sees one lever activation");
        }
    }

    if (failures == 0)
        spdlog::info("PASS steps 3 and 8: door and lever relayed to the peer; the late joiner sees the door open and one lever pull");
    return failures;
}

namespace
{
const ObjectData* FindObject(const AssignObjectsResponse& acResponse, const uint32_t aBaseId)
{
    for (const auto& object : acResponse.Objects)
        if (object.Id.BaseId == aBaseId)
            return &object;
    return nullptr;
}

int32_t CountOf(const Inventory& acInventory, const GameId& acItem)
{
    int32_t count = 0;
    for (const auto& entry : acInventory.Entries)
        if (entry.BaseId == acItem)
            count += entry.Count;
    return count;
}
} // namespace

int RunContainerTake(Bots& aBots, const std::string& acExpectationFile)
{
    constexpr int32_t kTaken = 3;
    Bot& taker = *aBots[0];
    Bot& peer = *aBots[1];

    const auto before = AssignFixtureObjects(aBots, taker);
    const ObjectData* pChest = before ? FindObject(*before, L2Fixture::kChestRef) : nullptr;
    if (!pChest || pChest->CurrentInventory.Entries.empty())
    {
        spdlog::error("CHECK failed: the taker sees the chest with contents");
        return 1;
    }
    const GameId item = pChest->CurrentInventory.Entries.front().BaseId;
    const int32_t countBefore = CountOf(pChest->CurrentInventory, item);
    const uint32_t chestServerId = pChest->ServerId;

    std::optional<uint8_t> result;
    taker.OnMessage = [&](const ServerMessage& acMessage)
    {
        if (acMessage.GetOpcode() == kNotifyContainerTransferResult)
            result = static_cast<const NotifyContainerTransferResult&>(acMessage).Result;
    };
    std::optional<int32_t> peerDelta;
    peer.OnMessage = [&](const ServerMessage& acMessage)
    {
        if (acMessage.GetOpcode() != kNotifyInventoryChanges)
            return;
        const auto& change = static_cast<const NotifyInventoryChanges&>(acMessage);
        if (change.ServerId == chestServerId)
            peerDelta = change.Item.Count;
    };

    RequestContainerTransfer take{};
    take.RequestId = 1;
    take.ContainerId = chestServerId;
    take.TargetKind = 0; // object
    take.Direction = 0;  // take
    take.ExpectedContainerCount = countBefore;
    take.Item.BaseId = item;
    take.Item.Count = kTaken;
    taker.Send(take);

    static_cast<void>(Pump(aBots, [&] { return result.has_value() && peerDelta.has_value(); }, std::chrono::seconds(5)));
    taker.OnMessage = nullptr;
    peer.OnMessage = nullptr;

    int failures = 0;
    failures += Check(result.has_value() && *result == 0, "the take is accepted");
    failures += Check(peerDelta.has_value() && *peerDelta == -kTaken, "the peer is told the chest lost the taken count");

    // What another player now sees in the chest.
    const auto after = AssignFixtureObjects(aBots, peer);
    const ObjectData* pAfter = after ? FindObject(*after, L2Fixture::kChestRef) : nullptr;
    failures += Check(pAfter && CountOf(pAfter->CurrentInventory, item) == countBefore - kTaken, "the chest holds the rest for the peer");

    std::ofstream expectation(acExpectationFile, std::ios::trunc);
    expectation << item.ModId << ' ' << item.BaseId << ' ' << (countBefore - kTaken) << '\n';
    failures += Check(static_cast<bool>(expectation), "the expectation file is written");

    if (failures == 0)
        spdlog::info("PASS step 4: took {} of {:X} from the chest ({} left), the peer was told", kTaken, item.BaseId, countBefore - kTaken);
    return failures;
}

int RunAfterRestart(Bots& aBots, const std::string& acExpectationFile)
{
    uint32_t modId = 0, baseId = 0;
    int32_t expected = 0;
    std::ifstream expectation(acExpectationFile);
    if (!(expectation >> modId >> baseId >> expected))
    {
        spdlog::error("CHECK failed: the expectation file {} is readable", acExpectationFile);
        return 1;
    }

    const auto response = AssignFixtureObjects(aBots, *aBots[0]);
    int failures = Check(response.has_value(), "the restarted server answers the assignment");
    if (!response)
        return failures;

    const ObjectData* pChest = FindObject(*response, L2Fixture::kChestRef);
    const int32_t count = pChest ? CountOf(pChest->CurrentInventory, GameId(modId, baseId)) : -1;
    if (count != expected)
        spdlog::error("chest holds {} of {:X} after the restart, expected {}", count, baseId, expected);
    failures += Check(count == expected, "the chest keeps the take across a restart");

    const ObjectData* pDoor = FindObject(*response, L2Fixture::kDoorRef);
    failures += Check(pDoor && pDoor->IsDoorStateKnown && pDoor->IsDoorOpen, "the door stays open across a restart");

    if (failures == 0)
        spdlog::info("PASS step 4 after restart: the chest holds {} and the door is open", expected);
    return failures;
}

int RunLeveledActor(Bots& aBots)
{
    // The fixture's leveled list has one entry, so the server's pick is always kBandit. Each bot
    // claims something else, as a client whose own roll differed would.
    const uint32_t claims[] = {0x000FFF, 0x000FFE};
    std::vector<std::optional<AssignCharacterResponse>> responses(aBots.size());
    bool rejected = false;

    for (size_t i = 0; i < 2; ++i)
    {
        Bot& bot = *aBots[i];
        const uint32_t cookie = 100 + static_cast<uint32_t>(i);
        bot.OnMessage = [&, i, cookie](const ServerMessage& acMessage)
        {
            if (acMessage.GetOpcode() == kAssignCharacterResponse)
            {
                const auto& response = static_cast<const AssignCharacterResponse&>(acMessage);
                if (response.Cookie == cookie)
                    responses[i] = response;
            }
            else if (acMessage.GetOpcode() == kNotifyCharacterAssignmentRejected)
                rejected = true;
        };

        const uint32_t modId = bot.GetFixtureModId();
        AssignCharacterRequest request{};
        request.Cookie = cookie;
        request.ReferenceId = GameId(modId, L2Fixture::kBanditRef);
        request.FormId = GameId(modId, L2Fixture::kBandit);
        request.LeveledNpcPickId = GameId(modId, claims[i]);
        request.CellId = GameId(modId, L2Fixture::kCell);
        bot.Send(request);

        static_cast<void>(Pump(aBots, [&] { return responses[i].has_value() || rejected; }, std::chrono::seconds(5)));
        bot.OnMessage = nullptr;
    }

    int failures = 0;
    failures += Check(!rejected, "the leveled actor assignment is not rejected");
    failures += Check(responses[0].has_value() && responses[1].has_value(), "both bots get an assignment response");
    if (failures)
        return failures;

    const GameId expectedPick(aBots[0]->GetFixtureModId(), L2Fixture::kBandit);
    for (size_t i = 0; i < 2; ++i)
    {
        if (responses[i]->LeveledNpcPickId != expectedPick)
            spdlog::error("[{}] got pick {:X}:{:X}, expected {:X}:{:X}", aBots[i]->GetName(), responses[i]->LeveledNpcPickId.ModId,
                responses[i]->LeveledNpcPickId.BaseId, expectedPick.ModId, expectedPick.BaseId);
        failures += Check(responses[i]->LeveledNpcPickId == expectedPick, "the server's pick overrides the client's claim");
    }
    failures += Check(responses[0]->ServerId != 0 && responses[0]->ServerId == responses[1]->ServerId, "both bots get the same actor");
    failures += Check(responses[0]->Owner && !responses[1]->Owner, "the first bot owns the actor, the second does not");

    if (failures == 0)
        spdlog::info("PASS step 5: both bots see the server's leveled pick {:X} for actor {}", L2Fixture::kBandit, responses[0]->ServerId);
    return failures;
}

int RunDeathAndRespawn(Bots& aBots)
{
    Bot& victim = *aBots[0];
    Bot& peer = *aBots[1];
    const uint32_t victimId = victim.GetServerId();
    const uint32_t epoch = victim.GetOwnershipEpoch();
    constexpr uint32_t kHealth = 24;

    bool sawZeroHealth = false;
    bool sawDead = false;
    std::optional<NotifyRespawn> respawn;
    std::map<uint32_t, float> restored;
    peer.OnMessage = [&](const ServerMessage& acMessage)
    {
        switch (acMessage.GetOpcode())
        {
        case kNotifyActorValueChanges:
        {
            const auto& notify = static_cast<const NotifyActorValueChanges&>(acMessage);
            if (notify.Id != victimId)
                break;
            for (const auto& [id, value] : notify.Values)
            {
                // The restored vitals go out just before NotifyRespawn, so count them from the death on.
                if (!sawDead && id == kHealth && value <= 0.f)
                    sawZeroHealth = true;
                else if (sawDead)
                    restored[id] = value;
            }
            break;
        }
        case kNotifyDeathStateChange:
        {
            const auto& notify = static_cast<const NotifyDeathStateChange&>(acMessage);
            if (notify.Id == victimId && notify.IsDead)
                sawDead = true;
            break;
        }
        case kNotifyRespawn:
        {
            const auto& notify = static_cast<const NotifyRespawn&>(acMessage);
            if (notify.ActorId == victimId)
                respawn = notify;
            break;
        }
        default: break;
        }
    };

    // The owner reports the killing blow, then its death.
    RequestActorValueChanges health{};
    health.Id = victimId;
    health.OwnershipEpoch = epoch;
    health.Values[kHealth] = 0.f;
    victim.Send(health);

    RequestDeathStateChange death{};
    death.Id = victimId;
    death.OwnershipEpoch = epoch;
    death.IsDead = true;
    victim.Send(death);
    static_cast<void>(Pump(aBots, [&] { return sawZeroHealth && sawDead; }, std::chrono::seconds(5)));

    int failures = 0;
    failures += Check(sawZeroHealth, "the peer sees the victim's health drop to 0");
    failures += Check(sawDead, "the peer sees the victim die");

    // The respawn: the server decides the new incarnation is alive at full vitals.
    victim.Send(PlayerRespawnRequest{});
    static_cast<void>(Pump(aBots, [&] { return respawn.has_value() && restored.size() == 3; }, std::chrono::seconds(5)));
    peer.OnMessage = nullptr;

    failures += Check(respawn.has_value(), "the peer is told the victim respawned");
    if (respawn)
        failures += Check(respawn->OwnershipEpoch != 0 && respawn->OwnershipEpoch == epoch, "the respawn carries the owner's current epoch");
    for (uint32_t i = 0; i < 3; ++i)
    {
        const auto it = restored.find(kHealth + i);
        if (it == restored.end() || it->second != kBotMaxVitals[i])
            spdlog::error("vital {} after respawn: {}, expected {}", kHealth + i, it == restored.end() ? -1.f : it->second, kBotMaxVitals[i]);
        failures += Check(it != restored.end() && it->second == kBotMaxVitals[i], "the peer sees the vital back at its maximum");
    }

    if (failures == 0)
        spdlog::info("PASS step 6: {} died and respawned; {} saw it dead, then alive at full vitals (epoch {})", victim.GetName(), peer.GetName(), epoch);
    return failures;
}
