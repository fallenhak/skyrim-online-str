#include "Scenario.h"

#include <fixture/L2Fixture.h>

#include <Messages/AssignObjectsRequest.h>
#include <Messages/ActivateRequest.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/NotifyActivate.h>

#include <spdlog/spdlog.h>

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
std::optional<AssignObjectsResponse> AssignDoorAndLever(Bots& aBots, Bot& aBot)
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

    const auto response = AssignDoorAndLever(aBots, *aBots.back());
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
