#pragma once

#include "Bot.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using Bots = std::vector<std::unique_ptr<Bot>>;

// Updates every bot until aDone returns true, a bot fails or the timeout passes.
[[nodiscard]] bool Pump(Bots& aBots, const std::function<bool()>& aDone, std::chrono::milliseconds aTimeout);

// Connects the bots that are not connected yet and waits until all of them are in the world.
[[nodiscard]] bool EnterWorld(Bots& aBots, const std::string& acEndpoint);

// Scenario steps (docs/L2_BOT_DESIGN.md). Each returns the number of failed checks.
[[nodiscard]] int RunAssignObjects(Bots& aBots);
// Steps 3 and 8: door and lever activations relay to the peer; a late joiner gets the final state.
// Step 4: a take from the chest is accepted, the peer is told, and the new contents are persisted.
// Writes what the restarted server must still have to acExpectationFile.
[[nodiscard]] int RunContainerTake(Bots& aBots, const std::string& acExpectationFile);
// Step 4, second half: after a server restart the chest keeps the take and the door stays open.
[[nodiscard]] int RunAfterRestart(Bots& aBots, const std::string& acExpectationFile);
// Step 5: the placed leveled actor gets the server's pick for every client, whatever they claim.
[[nodiscard]] int RunLeveledActor(Bots& aBots);
// Step 6: a bot dies and respawns; the peer sees it dead, then respawned alive at full vitals.
[[nodiscard]] int RunDeathAndRespawn(Bots& aBots);
// Step 7: a bow shot, whose casting source is not a spell slot, is accepted and relayed;
// a spell with an out-of-range casting source is not.
[[nodiscard]] int RunArrow(Bots& aBots);
[[nodiscard]] int RunActivations(Bots& aBots, const std::string& acEndpoint, const std::string& acSecret);
