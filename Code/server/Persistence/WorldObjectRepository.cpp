#include <Persistence/WorldObjectRepository.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <utility>

namespace Persistence
{
namespace
{
constexpr std::uint64_t kMaxStoredValue = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr auto kShutdownFlushTimeout = std::chrono::seconds(30);
constexpr auto kWriteRetryDelay = std::chrono::seconds(1);
// Matches ContainerContentsCodec::kMaxEncodedBytes: two hex digits per byte.
constexpr std::size_t kMaxContainerInventoryHex = (1u << 20) * 2;

[[nodiscard]] std::int64_t GetUnixTimestamp() noexcept
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

[[nodiscard]] bool IsBoolean(const std::int64_t aValue) noexcept
{
    return aValue == 0 || aValue == 1;
}

[[nodiscard]] bool IsUint32(const std::int64_t aValue) noexcept
{
    return aValue >= 0 && static_cast<std::uint64_t>(aValue) <= std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] bool IsInt32(const std::int64_t aValue) noexcept
{
    return aValue >= std::numeric_limits<std::int32_t>::min() && aValue <= std::numeric_limits<std::int32_t>::max();
}

[[nodiscard]] std::size_t CombineHash(const std::size_t aSeed, const std::size_t aValue) noexcept
{
    return aSeed ^ (aValue + static_cast<std::size_t>(0x9e3779b9) + (aSeed << 6) + (aSeed >> 2));
}
} // namespace

bool WorldObjectRepository::Key::operator==(const Key& acRhs) const noexcept
{
    return ObjectModId == acRhs.ObjectModId && ObjectBaseId == acRhs.ObjectBaseId &&
        CellModId == acRhs.CellModId && CellBaseId == acRhs.CellBaseId;
}

std::size_t WorldObjectRepository::KeyHash::operator()(const Key& acKey) const noexcept
{
    std::size_t seed = std::hash<std::uint32_t>{}(acKey.ObjectModId);
    seed = CombineHash(seed, std::hash<std::uint32_t>{}(acKey.ObjectBaseId));
    seed = CombineHash(seed, std::hash<std::uint32_t>{}(acKey.CellModId));
    return CombineHash(seed, std::hash<std::uint32_t>{}(acKey.CellBaseId));
}

WorldObjectRepository::Key WorldObjectRepository::MakeKey(const GameId& acId, const GameId& acCellId) noexcept
{
    return {acId.ModId, acId.BaseId, acCellId.ModId, acCellId.BaseId};
}

WorldObjectRepository::WorldObjectRepository(Database& aDatabase)
    : m_database(aDatabase)
    , m_writer([this] { RunWriter(); })
{
}

WorldObjectRepository::~WorldObjectRepository() noexcept
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stopping = true;
        m_shutdownDeadline = std::chrono::steady_clock::now() + kShutdownFlushTimeout;
    }
    m_queueChanged.notify_one();
    if (m_writer.joinable())
        m_writer.join();
}

bool WorldObjectRepository::IsValid(const WorldObjectState& acState) noexcept
{
    if (!acState.Id.BaseId || !acState.CellId.BaseId ||
        (acState.WorldSpaceId.ModId != 0 && acState.WorldSpaceId.BaseId == 0) ||
        acState.HarvestRespawnAtUnix > kMaxStoredValue || acState.LootRespawnAtUnix > kMaxStoredValue)
        return false;

    if (acState.IsDoor != acState.DoorStateKnown || (acState.DoorIsOpen && !acState.DoorStateKnown))
        return false;
    if (acState.IsActivator != (acState.ActivationCount != 0))
        return false;
    if (acState.IsHarvestItem && !acState.IsHarvestable)
        return false;
    if (acState.IsHarvestable != acState.IsHarvested || acState.IsOpenLoot != acState.IsLootTaken)
        return false;
    if (acState.IsHarvested != (acState.HarvestRespawnAtUnix != 0) || (acState.IsHarvested && !acState.IsHarvestable))
        return false;
    if (acState.IsLootTaken != (acState.LootRespawnAtUnix != 0) || (acState.IsLootTaken && !acState.IsOpenLoot))
        return false;
    if (acState.WorldSpaceId && (acState.CenterCoords.X == std::numeric_limits<std::int32_t>::max() ||
                                 acState.CenterCoords.Y == std::numeric_limits<std::int32_t>::max()))
        return false;

    const int changedStateTypes = static_cast<int>(acState.DoorStateKnown) + static_cast<int>(acState.ActivationCount != 0) +
        static_cast<int>(acState.IsHarvested) + static_cast<int>(acState.IsLootTaken);
    if (changedStateTypes > 1)
        return false;

    return changedStateTypes == 1;
}

bool WorldObjectRepository::IsValid(const ContainerContentsState& acState) noexcept
{
    if (!acState.Id.BaseId || !acState.CellId.BaseId || (acState.WorldSpaceId.ModId != 0 && acState.WorldSpaceId.BaseId == 0))
        return false;
    if (acState.InventoryHex.empty() || acState.InventoryHex.size() > kMaxContainerInventoryHex || acState.InventoryHex.size() % 2 != 0)
        return false;
    return std::all_of(acState.InventoryHex.begin(), acState.InventoryHex.end(),
        [](const char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); });
}

std::vector<WorldObjectState> WorldObjectRepository::LoadAll() const
{
    std::vector<WorldObjectState> records;
    auto statement = m_database.Prepare(
        "SELECT object_mod_id, object_base_id, cell_mod_id, cell_base_id, worldspace_mod_id, worldspace_base_id, "
        "center_x, center_y, is_door, door_is_open, activation_count, is_harvestable, is_harvest_item, is_harvested, "
        "harvest_respawn_at_unix, is_open_loot, is_loot_taken, loot_respawn_at_unix "
        "FROM world_objects ORDER BY object_mod_id, object_base_id, cell_mod_id, cell_base_id;");

    while (statement.Step())
    {
        const auto objectMod = statement.ColumnInt64(0);
        const auto objectBase = statement.ColumnInt64(1);
        const auto cellMod = statement.ColumnInt64(2);
        const auto cellBase = statement.ColumnInt64(3);
        const auto worldSpaceMod = statement.ColumnInt64(4);
        const auto worldSpaceBase = statement.ColumnInt64(5);
        const auto centerX = statement.ColumnInt64(6);
        const auto centerY = statement.ColumnInt64(7);
        const auto isDoor = statement.ColumnInt64(8);
        const auto doorIsOpen = statement.ColumnInt64(9);
        const auto activationCount = statement.ColumnInt64(10);
        const auto isHarvestable = statement.ColumnInt64(11);
        const auto isHarvestItem = statement.ColumnInt64(12);
        const auto isHarvested = statement.ColumnInt64(13);
        const auto harvestRespawnAt = statement.ColumnInt64(14);
        const auto isOpenLoot = statement.ColumnInt64(15);
        const auto isLootTaken = statement.ColumnInt64(16);
        const auto lootRespawnAt = statement.ColumnInt64(17);

        if (!IsUint32(objectMod) || objectBase <= 0 || !IsUint32(objectBase) || !IsUint32(cellMod) ||
            cellBase <= 0 || !IsUint32(cellBase) || !IsUint32(worldSpaceMod) || !IsUint32(worldSpaceBase) ||
            !IsInt32(centerX) || !IsInt32(centerY) || !IsBoolean(isDoor) || !IsBoolean(doorIsOpen) ||
            !IsUint32(activationCount) || !IsBoolean(isHarvestable) || !IsBoolean(isHarvestItem) ||
            !IsBoolean(isHarvested) || harvestRespawnAt < 0 || !IsBoolean(isOpenLoot) ||
            !IsBoolean(isLootTaken) || lootRespawnAt < 0)
        {
            spdlog::warn("[Persistence] Skipped malformed world object row {:x}:{:x} cell {:x}:{:x}", objectMod, objectBase, cellMod, cellBase);
            continue;
        }

        WorldObjectState state{};
        state.Id = GameId(static_cast<std::uint32_t>(objectMod), static_cast<std::uint32_t>(objectBase));
        state.CellId = GameId(static_cast<std::uint32_t>(cellMod), static_cast<std::uint32_t>(cellBase));
        state.WorldSpaceId = GameId(static_cast<std::uint32_t>(worldSpaceMod), static_cast<std::uint32_t>(worldSpaceBase));
        state.CenterCoords = GridCellCoords(static_cast<std::int32_t>(centerX), static_cast<std::int32_t>(centerY));
        state.IsDoor = isDoor == 1;
        state.DoorStateKnown = state.IsDoor;
        state.DoorIsOpen = doorIsOpen == 1;
        state.ActivationCount = static_cast<std::uint32_t>(activationCount);
        state.IsActivator = state.ActivationCount != 0;
        state.IsHarvestable = isHarvestable == 1;
        state.IsHarvestItem = isHarvestItem == 1;
        state.IsHarvested = isHarvested == 1;
        state.HarvestRespawnAtUnix = static_cast<std::uint64_t>(harvestRespawnAt);
        state.IsOpenLoot = isOpenLoot == 1;
        state.IsLootTaken = isLootTaken == 1;
        state.LootRespawnAtUnix = static_cast<std::uint64_t>(lootRespawnAt);

        if (!IsValid(state))
        {
            spdlog::warn("[Persistence] Skipped invalid world object state {:x}:{:x} cell {:x}:{:x}", objectMod, objectBase, cellMod, cellBase);
            continue;
        }

        records.push_back(state);
    }

    return records;
}

std::vector<ContainerContentsState> WorldObjectRepository::LoadAllContainers() const
{
    std::vector<ContainerContentsState> records;
    auto statement = m_database.Prepare(
        "SELECT object_mod_id, object_base_id, cell_mod_id, cell_base_id, worldspace_mod_id, worldspace_base_id, center_x, center_y, inventory "
        "FROM container_contents ORDER BY object_mod_id, object_base_id, cell_mod_id, cell_base_id;");

    while (statement.Step())
    {
        const auto objectMod = statement.ColumnInt64(0);
        const auto objectBase = statement.ColumnInt64(1);
        const auto cellMod = statement.ColumnInt64(2);
        const auto cellBase = statement.ColumnInt64(3);
        const auto worldSpaceMod = statement.ColumnInt64(4);
        const auto worldSpaceBase = statement.ColumnInt64(5);
        const auto centerX = statement.ColumnInt64(6);
        const auto centerY = statement.ColumnInt64(7);

        if (!IsUint32(objectMod) || objectBase <= 0 || !IsUint32(objectBase) || !IsUint32(cellMod) || cellBase <= 0 || !IsUint32(cellBase) ||
            !IsUint32(worldSpaceMod) || !IsUint32(worldSpaceBase) || !IsInt32(centerX) || !IsInt32(centerY))
        {
            spdlog::warn("[Persistence] Skipped malformed container row {:x}:{:x} cell {:x}:{:x}", objectMod, objectBase, cellMod, cellBase);
            continue;
        }

        ContainerContentsState state{};
        state.Id = GameId(static_cast<std::uint32_t>(objectMod), static_cast<std::uint32_t>(objectBase));
        state.CellId = GameId(static_cast<std::uint32_t>(cellMod), static_cast<std::uint32_t>(cellBase));
        state.WorldSpaceId = GameId(static_cast<std::uint32_t>(worldSpaceMod), static_cast<std::uint32_t>(worldSpaceBase));
        state.CenterCoords = GridCellCoords(static_cast<std::int32_t>(centerX), static_cast<std::int32_t>(centerY));
        state.InventoryHex = statement.ColumnText(8);

        if (!IsValid(state))
        {
            spdlog::warn("[Persistence] Skipped invalid container contents {:x}:{:x} cell {:x}:{:x}", objectMod, objectBase, cellMod, cellBase);
            continue;
        }

        records.push_back(std::move(state));
    }

    return records;
}

void WorldObjectRepository::EnqueueContainerUpsert(const ContainerContentsState& acState) noexcept
{
    if (!IsValid(acState))
    {
        spdlog::warn("[Persistence] Rejected invalid container contents {:x}:{:x} cell {:x}:{:x}",
            acState.Id.ModId, acState.Id.BaseId, acState.CellId.ModId, acState.CellId.BaseId);
        return;
    }

    try
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_stopping)
                return;
            m_pendingContainers[MakeKey(acState.Id, acState.CellId)] = acState;
        }
        m_queueChanged.notify_one();
    }
    catch (const std::exception& exception)
    {
        spdlog::error("[Persistence] Could not queue container contents write: {}", exception.what());
    }
}

void WorldObjectRepository::EnqueueUpsert(const WorldObjectState& acState) noexcept
{
    if (!IsValid(acState))
    {
        spdlog::warn("[Persistence] Rejected invalid world object state {:x}:{:x} cell {:x}:{:x}",
            acState.Id.ModId, acState.Id.BaseId, acState.CellId.ModId, acState.CellId.BaseId);
        return;
    }

    try
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_stopping)
                return;
            m_pending[MakeKey(acState.Id, acState.CellId)] = acState;
        }
        m_queueChanged.notify_one();
    }
    catch (const std::exception& exception)
    {
        spdlog::error("[Persistence] Could not queue world object write: {}", exception.what());
    }
}

void WorldObjectRepository::EnqueueDelete(const GameId& acId, const GameId& acCellId) noexcept
{
    if (!acId.BaseId || !acCellId.BaseId)
        return;

    try
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_stopping)
                return;
            m_pending[MakeKey(acId, acCellId)] = std::nullopt;
        }
        m_queueChanged.notify_one();
    }
    catch (const std::exception& exception)
    {
        spdlog::error("[Persistence] Could not queue world object deletion: {}", exception.what());
    }
}

void WorldObjectRepository::RunWriter() noexcept
{
    for (;;)
    {
        PendingBatch batch;
        PendingContainerBatch containerBatch;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueChanged.wait(lock, [this] { return m_stopping || !m_pending.empty() || !m_pendingContainers.empty(); });
            if (m_pending.empty() && m_pendingContainers.empty() && m_stopping)
                return;
            batch.swap(m_pending);
            containerBatch.swap(m_pendingContainers);
        }

        try
        {
            WriteBatch(batch, containerBatch);
            continue;
        }
        catch (const std::exception& exception)
        {
            spdlog::error("[Persistence] World object batch write failed: {}", exception.what());
        }
        catch (...)
        {
            spdlog::error("[Persistence] World object batch write failed with an unknown error");
        }

        std::unique_lock<std::mutex> lock(m_queueMutex);
        for (auto& [key, write] : batch)
        {
            if (!m_pending.contains(key))
                m_pending.emplace(key, std::move(write));
        }
        for (auto& [key, write] : containerBatch)
        {
            if (!m_pendingContainers.contains(key))
                m_pendingContainers.emplace(key, std::move(write));
        }

        if (m_stopping && std::chrono::steady_clock::now() >= m_shutdownDeadline)
        {
            spdlog::error(
                "[Persistence] Shutdown flush timed out after 30 seconds; {} queued world object write(s) could not be saved",
                m_pending.size() + m_pendingContainers.size());
            return;
        }

        auto retryDelay = std::chrono::duration_cast<std::chrono::milliseconds>(kWriteRetryDelay);
        if (m_stopping)
        {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(m_shutdownDeadline - std::chrono::steady_clock::now());
            retryDelay = std::min(retryDelay, remaining);
        }
        if (retryDelay > std::chrono::steady_clock::duration::zero())
            m_queueChanged.wait_for(lock, retryDelay);
    }
}

void WorldObjectRepository::WriteBatch(const PendingBatch& acBatch, const PendingContainerBatch& acContainerBatch)
{
    const auto updatedAt = GetUnixTimestamp();

    for (const auto& [key, write] : acBatch)
    {
        if (!write)
        {
            auto statement = m_database.Prepare(
                "DELETE FROM world_objects WHERE object_mod_id = ?1 AND object_base_id = ?2 AND cell_mod_id = ?3 AND cell_base_id = ?4;");
            statement.Bind(1, static_cast<std::int64_t>(key.ObjectModId));
            statement.Bind(2, static_cast<std::int64_t>(key.ObjectBaseId));
            statement.Bind(3, static_cast<std::int64_t>(key.CellModId));
            statement.Bind(4, static_cast<std::int64_t>(key.CellBaseId));
            (void)statement.Step();
            continue;
        }

        const auto& state = *write;
        auto statement = m_database.Prepare(
            "INSERT INTO world_objects (object_mod_id, object_base_id, cell_mod_id, cell_base_id, worldspace_mod_id, worldspace_base_id, "
            "center_x, center_y, is_door, door_is_open, activation_count, is_harvestable, is_harvest_item, is_harvested, "
            "harvest_respawn_at_unix, is_open_loot, is_loot_taken, loot_respawn_at_unix, updated_at) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19) "
            "ON CONFLICT (object_mod_id, object_base_id, cell_mod_id, cell_base_id) DO UPDATE SET "
            "worldspace_mod_id = excluded.worldspace_mod_id, worldspace_base_id = excluded.worldspace_base_id, "
            "center_x = excluded.center_x, center_y = excluded.center_y, is_door = excluded.is_door, "
            "door_is_open = excluded.door_is_open, activation_count = excluded.activation_count, "
            "is_harvestable = excluded.is_harvestable, is_harvest_item = excluded.is_harvest_item, "
            "is_harvested = excluded.is_harvested, harvest_respawn_at_unix = excluded.harvest_respawn_at_unix, "
            "is_open_loot = excluded.is_open_loot, is_loot_taken = excluded.is_loot_taken, "
            "loot_respawn_at_unix = excluded.loot_respawn_at_unix, updated_at = excluded.updated_at;");
        statement.Bind(1, static_cast<std::int64_t>(state.Id.ModId));
        statement.Bind(2, static_cast<std::int64_t>(state.Id.BaseId));
        statement.Bind(3, static_cast<std::int64_t>(state.CellId.ModId));
        statement.Bind(4, static_cast<std::int64_t>(state.CellId.BaseId));
        statement.Bind(5, static_cast<std::int64_t>(state.WorldSpaceId.ModId));
        statement.Bind(6, static_cast<std::int64_t>(state.WorldSpaceId.BaseId));
        statement.Bind(7, static_cast<std::int64_t>(state.CenterCoords.X));
        statement.Bind(8, static_cast<std::int64_t>(state.CenterCoords.Y));
        statement.Bind(9, static_cast<std::int64_t>(state.IsDoor ? 1 : 0));
        statement.Bind(10, static_cast<std::int64_t>(state.DoorIsOpen ? 1 : 0));
        statement.Bind(11, static_cast<std::int64_t>(state.ActivationCount));
        statement.Bind(12, static_cast<std::int64_t>(state.IsHarvestable ? 1 : 0));
        statement.Bind(13, static_cast<std::int64_t>(state.IsHarvestItem ? 1 : 0));
        statement.Bind(14, static_cast<std::int64_t>(state.IsHarvested ? 1 : 0));
        statement.Bind(15, static_cast<std::int64_t>(state.HarvestRespawnAtUnix));
        statement.Bind(16, static_cast<std::int64_t>(state.IsOpenLoot ? 1 : 0));
        statement.Bind(17, static_cast<std::int64_t>(state.IsLootTaken ? 1 : 0));
        statement.Bind(18, static_cast<std::int64_t>(state.LootRespawnAtUnix));
        statement.Bind(19, updatedAt);
        (void)statement.Step();
    }

    for (const auto& [key, state] : acContainerBatch)
    {
        auto statement = m_database.Prepare(
            "INSERT INTO container_contents (object_mod_id, object_base_id, cell_mod_id, cell_base_id, worldspace_mod_id, worldspace_base_id, "
            "center_x, center_y, inventory, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10) "
            "ON CONFLICT (object_mod_id, object_base_id, cell_mod_id, cell_base_id) DO UPDATE SET "
            "worldspace_mod_id = excluded.worldspace_mod_id, worldspace_base_id = excluded.worldspace_base_id, "
            "center_x = excluded.center_x, center_y = excluded.center_y, inventory = excluded.inventory, updated_at = excluded.updated_at;");
        statement.Bind(1, static_cast<std::int64_t>(key.ObjectModId));
        statement.Bind(2, static_cast<std::int64_t>(key.ObjectBaseId));
        statement.Bind(3, static_cast<std::int64_t>(key.CellModId));
        statement.Bind(4, static_cast<std::int64_t>(key.CellBaseId));
        statement.Bind(5, static_cast<std::int64_t>(state.WorldSpaceId.ModId));
        statement.Bind(6, static_cast<std::int64_t>(state.WorldSpaceId.BaseId));
        statement.Bind(7, static_cast<std::int64_t>(state.CenterCoords.X));
        statement.Bind(8, static_cast<std::int64_t>(state.CenterCoords.Y));
        statement.Bind(9, std::string_view(state.InventoryHex));
        statement.Bind(10, updatedAt);
        (void)statement.Step();
    }
}
} // namespace Persistence
