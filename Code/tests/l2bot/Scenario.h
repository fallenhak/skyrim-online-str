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
[[nodiscard]] int RunActivations(Bots& aBots, const std::string& acEndpoint, const std::string& acSecret);
