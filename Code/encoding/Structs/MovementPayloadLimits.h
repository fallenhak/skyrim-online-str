#pragma once

#include <cstddef>

namespace MovementPayloadLimits
{
// A client normally sends one entry per actor it owns. Keep enough headroom for
// creature-heavy sessions while preventing an unbounded map allocation.
constexpr std::size_t kMaxUpdates = 256;

// Action history is replayed for animation presentation and is already kept in
// a small rolling cache on the server.
constexpr std::size_t kMaxActionEvents = 64;
} // namespace MovementPayloadLimits
