#pragma once

#include <Persistence/Database.h>

#include <Structs/GameId.h>
#include <Structs/GridCellCoords.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Persistence
{
struct WorldObjectState final
{
    GameId Id{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GridCellCoords CenterCoords{};

    bool IsDoor{};
    bool DoorStateKnown{};
    bool DoorIsOpen{};
    bool IsActivator{};
    std::uint32_t ActivationCount{};
    bool IsHarvestable{};
    bool IsHarvestItem{};
    bool IsHarvested{};
    std::uint64_t HarvestRespawnAtUnix{};
    bool IsOpenLoot{};
    bool IsLootTaken{};
    std::uint64_t LootRespawnAtUnix{};
};

// Server-owned contents of a synced container. Inventory is opaque here (hex text written
// by ContainerContentsCodec); the repository only checks that it is well-formed hex.
struct ContainerContentsState final
{
    GameId Id{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GridCellCoords CenterCoords{};
    std::string InventoryHex{};
};

struct WorldObjectRepository final
{
    explicit WorldObjectRepository(Database& aDatabase);
    ~WorldObjectRepository() noexcept;

    WorldObjectRepository(const WorldObjectRepository&) = delete;
    WorldObjectRepository& operator=(const WorldObjectRepository&) = delete;
    WorldObjectRepository(WorldObjectRepository&&) = delete;
    WorldObjectRepository& operator=(WorldObjectRepository&&) = delete;

    // Called at startup before clients connect.
    [[nodiscard]] std::vector<WorldObjectState> LoadAll() const;

    // Gameplay only copies into an in-memory queue. A background worker coalesces
    // pending changes and performs SQLite I/O off the server's update thread.
    void EnqueueUpsert(const WorldObjectState& acState) noexcept;
    void EnqueueDelete(const GameId& acId, const GameId& acCellId) noexcept;

    // Containers keep their own table: a chest's contents are not one of the mutually
    // exclusive world_objects state types. Writes share the same background worker.
    [[nodiscard]] std::vector<ContainerContentsState> LoadAllContainers() const;
    void EnqueueContainerUpsert(const ContainerContentsState& acState) noexcept;

private:
    struct Key final
    {
        std::uint32_t ObjectModId{};
        std::uint32_t ObjectBaseId{};
        std::uint32_t CellModId{};
        std::uint32_t CellBaseId{};

        bool operator==(const Key& acRhs) const noexcept;
    };

    struct KeyHash final
    {
        std::size_t operator()(const Key& acKey) const noexcept;
    };

    using PendingWrite = std::optional<WorldObjectState>;
    using PendingBatch = std::unordered_map<Key, PendingWrite, KeyHash>;
    using PendingContainerBatch = std::unordered_map<Key, ContainerContentsState, KeyHash>;

    [[nodiscard]] static Key MakeKey(const GameId& acId, const GameId& acCellId) noexcept;
    [[nodiscard]] static bool IsValid(const WorldObjectState& acState) noexcept;
    [[nodiscard]] static bool IsValid(const ContainerContentsState& acState) noexcept;
    void RunWriter() noexcept;
    void WriteBatch(const PendingBatch& acBatch, const PendingContainerBatch& acContainerBatch);

    Database& m_database;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueChanged;
    PendingBatch m_pending;
    PendingContainerBatch m_pendingContainers;
    bool m_stopping{};
    std::chrono::steady_clock::time_point m_shutdownDeadline{};
    std::thread m_writer;
};
} // namespace Persistence
