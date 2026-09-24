#pragma once

#include <Structs/Inventory.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

enum class ContainerTransferDirection : uint8_t
{
    kTake = 0, // container -> player
    kPut = 1,  // player -> container
};

enum class ContainerTransferResult : uint8_t
{
    kAccepted = 0,
    kStale = 1,        // the sender saw a count the server has already moved past
    kInsufficient = 2, // the container does not hold that many
    kNotAllowed = 3,   // cell/range/character/baseline checks failed
    kRateLimited = 4,
    kInvalid = 5,      // malformed item or direction
};

// Per-player transfer bookkeeping: a fixed-size replay cache (idempotent RequestId)
// and a one-second rate window.
struct ContainerTransferSession
{
    static constexpr std::size_t kReplayCacheSize = 64;

    struct Replay
    {
        uint32_t RequestId{};
        ContainerTransferResult Result{};
        bool Used{};
    };

    std::array<Replay, kReplayCacheSize> Replays{};
    std::size_t NextReplay{};
    uint64_t WindowSecond{};
    uint32_t TransfersInWindow{};
};

struct ContainerTransferPolicy final
{
    // "Take all" on a full chest sends one transfer per item stack in the same second.
    static constexpr uint32_t kMaxTransfersPerSecond = 64;

    [[nodiscard]] static int64_t CountOf(const Inventory& acInventory, const Inventory::Entry& acItem) noexcept
    {
        int64_t count = 0;
        for (const auto& entry : acInventory.Entries)
        {
            if (entry.CanBeMerged(acItem))
                count += entry.Count;
        }
        return count;
    }

    [[nodiscard]] static const ContainerTransferResult* FindReplay(const ContainerTransferSession& acSession, const uint32_t aRequestId) noexcept
    {
        for (const auto& replay : acSession.Replays)
        {
            if (replay.Used && replay.RequestId == aRequestId)
                return &replay.Result;
        }
        return nullptr;
    }

    static void RememberReplay(ContainerTransferSession& aSession, const uint32_t aRequestId, const ContainerTransferResult aResult) noexcept
    {
        aSession.Replays[aSession.NextReplay] = {aRequestId, aResult, true};
        aSession.NextReplay = (aSession.NextReplay + 1) % ContainerTransferSession::kReplayCacheSize;
    }

    [[nodiscard]] static bool ConsumeRate(ContainerTransferSession& aSession, const uint64_t aNowSecond) noexcept
    {
        if (aSession.WindowSecond != aNowSecond)
        {
            aSession.WindowSecond = aNowSecond;
            aSession.TransfersInWindow = 0;
        }

        if (aSession.TransfersInWindow >= kMaxTransfersPerSecond)
            return false;

        ++aSession.TransfersInWindow;
        return true;
    }

    // Moves acItem.Count units between the container and the player's inventory in one step.
    // aExpectedContainerCount is how many the sender saw in the container before the move; if the
    // server holds a different count the request is stale and nothing changes. The container is
    // the shared, server-owned side, so it carries the checks; the player's own inventory is
    // owner-reported anyway, and a put that exceeds the server's copy just empties that entry. A repeated
    // RequestId returns the first result without applying again; apApplied tells the caller
    // whether this call changed state (and must be relayed).
    [[nodiscard]] static ContainerTransferResult TryTransfer(
        ContainerTransferSession& aSession,
        const uint32_t aRequestId,
        const uint64_t aNowSecond,
        const bool aAllowed,
        const ContainerTransferDirection aDirection,
        const Inventory::Entry& acItem,
        const int32_t aExpectedContainerCount,
        Inventory& aContainer,
        Inventory& aPlayer,
        bool* apApplied = nullptr) noexcept
    {
        if (apApplied)
            *apApplied = false;

        if (const auto* pReplay = FindReplay(aSession, aRequestId))
            return *pReplay;

        const auto result = Evaluate(aSession, aNowSecond, aAllowed, aDirection, acItem, aExpectedContainerCount, aContainer, aPlayer);
        RememberReplay(aSession, aRequestId, result);
        if (apApplied)
            *apApplied = result == ContainerTransferResult::kAccepted;
        return result;
    }

private:
    [[nodiscard]] static ContainerTransferResult Evaluate(
        ContainerTransferSession& aSession,
        const uint64_t aNowSecond,
        const bool aAllowed,
        const ContainerTransferDirection aDirection,
        const Inventory::Entry& acItem,
        const int32_t aExpectedContainerCount,
        Inventory& aContainer,
        Inventory& aPlayer) noexcept
    {
        if (acItem.BaseId == GameId{} || acItem.Count <= 0 || aExpectedContainerCount < 0 ||
            (aDirection != ContainerTransferDirection::kTake && aDirection != ContainerTransferDirection::kPut))
            return ContainerTransferResult::kInvalid;

        if (!ConsumeRate(aSession, aNowSecond))
            return ContainerTransferResult::kRateLimited;

        if (!aAllowed)
            return ContainerTransferResult::kNotAllowed;

        const bool isTake = aDirection == ContainerTransferDirection::kTake;
        Inventory& source = isTake ? aContainer : aPlayer;
        Inventory& target = isTake ? aPlayer : aContainer;

        const int64_t containerCount = CountOf(aContainer, acItem);
        if (containerCount != aExpectedContainerCount)
            return ContainerTransferResult::kStale;

        if (isTake && containerCount < acItem.Count)
            return ContainerTransferResult::kInsufficient;

        if (CountOf(target, acItem) + acItem.Count > std::numeric_limits<int32_t>::max())
            return ContainerTransferResult::kInvalid;

        Inventory::Entry removed = acItem;
        removed.Count = -acItem.Count;
        source.AddOrRemoveEntry(removed);
        target.AddOrRemoveEntry(acItem);
        return ContainerTransferResult::kAccepted;
    }
};
