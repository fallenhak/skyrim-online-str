#include "Bot.h"
#include "Scenario.h"

#include <fixture/L2Fixture.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// L2 bot (#91): scripted clients against a real server, no game.
//   L2Bot prepare <server dir>       write Data/ (fixture + loadorder.txt) and config/STServer.ini
//   L2Bot connect <host:port>        two bots connect, enter the fixture cell and run the scenario
//   L2Bot after-restart <host:port>  one bot comes back after a server restart and checks persistence
// The server and the bots share SOS_AUTH_HMAC_SECRET; the bots sign their own session tokens.
namespace
{
constexpr auto kTimeout = std::chrono::seconds(30);
// What the restarted server must still have; written by connect, read by after-restart.
constexpr const char* kExpectationFile = "l2bot-expect.txt";

int Usage()
{
    spdlog::error("usage: L2Bot prepare <server dir> | L2Bot connect <host:port> | L2Bot after-restart <host:port>");
    return 2;
}

int Prepare(const std::filesystem::path& acServerDirectory)
{
    if (!L2Fixture::WriteDataDirectory(acServerDirectory / "Data"))
    {
        spdlog::error("could not write the fixture to {}", (acServerDirectory / "Data").string());
        return 1;
    }

    // Settings missing from the file keep their defaults. The fixture's records are only read
    // with actor record loading on.
    std::error_code error;
    std::filesystem::create_directories(acServerDirectory / "config", error);
    std::ofstream ini(acServerDirectory / "config" / "STServer.ini", std::ios::trunc);
    ini << "[LiveServices]\nbAnnounceServer=false\n\n"
           "[Population]\nbEnableActorRecordLoading=true\n\n"
           "[GameServer]\nuPort=10578\n";
    if (!ini)
    {
        spdlog::error("could not write config/STServer.ini");
        return 1;
    }

    spdlog::info("prepared {} with {}", acServerDirectory.string(), L2Fixture::kPluginName);
    return 0;
}

int Connect(const std::string& acEndpoint)
{
    const char* const pSecret = std::getenv("SOS_AUTH_HMAC_SECRET");
    if (!pSecret || !*pSecret)
    {
        spdlog::error("SOS_AUTH_HMAC_SECRET is not set");
        return 2;
    }

    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<Bot>(Bot::Config{"Alfa", 910000000000000001ull, pSecret, L2Fixture::kCell}));
    bots.push_back(std::make_unique<Bot>(Bot::Config{"Bravo", 910000000000000002ull, pSecret, L2Fixture::kCell}));

    for (auto& pBot : bots)
    {
        if (!pBot->Connect(acEndpoint))
        {
            spdlog::error("[{}] could not start connecting to {}", pBot->GetName(), acEndpoint);
            return 1;
        }
    }

    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        bool allInWorld = true;
        for (auto& pBot : bots)
        {
            pBot->Update();
            if (pBot->GetPhase() == Bot::Phase::kFailed)
                return 1;
            allInWorld = allInWorld && pBot->GetPhase() == Bot::Phase::kInWorld;
        }
        if (allInWorld)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    int failures = 0;
    for (const auto& pBot : bots)
    {
        if (pBot->GetPhase() != Bot::Phase::kInWorld)
        {
            spdlog::error("[{}] timed out while {}", pBot->GetName(), ToString(pBot->GetPhase()));
            ++failures;
        }
        else if (pBot->GetServerId() == 0 || pBot->GetCharacterId() == 0)
        {
            spdlog::error("[{}] in world without a server id or character id", pBot->GetName());
            ++failures;
        }
    }
    if (failures == 0 && bots[0]->GetServerId() == bots[1]->GetServerId())
    {
        spdlog::error("both bots got the same server id {:x}", bots[0]->GetServerId());
        ++failures;
    }

    // Let the server handle the cell entries before the bots leave, so their log lines land.
    const auto settle = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (failures == 0 && std::chrono::steady_clock::now() < settle)
    {
        for (auto& pBot : bots)
        {
            pBot->Update();
            if (pBot->GetPhase() == Bot::Phase::kFailed)
                ++failures;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (failures == 0)
    {
        spdlog::info("PASS step 1: both bots are in the fixture cell");
        failures += RunAssignObjects(bots);
        if (failures == 0)
            failures += RunContainerTake(bots, kExpectationFile);
        if (failures == 0)
            failures += RunLeveledActor(bots);
        if (failures == 0)
            failures += RunDeathAndRespawn(bots);
        if (failures == 0)
            failures += RunActivations(bots, acEndpoint, pSecret);
    }

    for (auto& pBot : bots)
        pBot->Shutdown();

    if (failures == 0)
        spdlog::info("PASS");
    return failures == 0 ? 0 : 1;
}
int AfterRestart(const std::string& acEndpoint)
{
    const char* const pSecret = std::getenv("SOS_AUTH_HMAC_SECRET");
    if (!pSecret || !*pSecret)
    {
        spdlog::error("SOS_AUTH_HMAC_SECRET is not set");
        return 2;
    }

    // Same identity as before the restart: the bot selects its existing character.
    Bots bots;
    bots.push_back(std::make_unique<Bot>(Bot::Config{"Alfa", 910000000000000001ull, pSecret, L2Fixture::kCell}));
    int failures = EnterWorld(bots, acEndpoint) ? 0 : 1;
    if (failures == 0)
        failures += RunAfterRestart(bots, kExpectationFile);

    for (auto& pBot : bots)
        pBot->Shutdown();
    if (failures == 0)
        spdlog::info("PASS");
    return failures == 0 ? 0 : 1;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
        return Usage();

    const std::string command = argv[1];
    if (command == "prepare")
        return Prepare(argv[2]);
    if (command == "connect")
        return Connect(argv[2]);
    if (command == "after-restart")
        return AfterRestart(argv[2]);
    return Usage();
}
