#pragma once

#include "Bot.h"

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

using Bots = std::vector<std::unique_ptr<Bot>>;

// Updates every bot until aDone returns true, a bot fails or the timeout passes.
[[nodiscard]] bool Pump(Bots& aBots, const std::function<bool()>& aDone, std::chrono::milliseconds aTimeout);

// Scenario steps (docs/L2_BOT_DESIGN.md). Each returns the number of failed checks.
[[nodiscard]] int RunAssignObjects(Bots& aBots);
