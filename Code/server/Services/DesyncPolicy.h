#pragma once

#include <Structs/ObjectStateDigest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

// Desync detector (world-state plan, phase 0). Compares a client's digest with
// the server's record of the same reference. Only fields the server owns are
// compared; the result is logged, never applied.
struct DesyncPolicy final
{
    enum class Field : uint8_t
    {
        kHarvested,
        kLootTaken,
        kLock,
        kDoor,
        kInventory,
    };

    // The server-owned part of an ObjectComponent/InventoryComponent pair.
    struct ServerView
    {
        bool HasTrustedState{};
        bool IsHarvestable{};
        bool IsHarvested{};
        bool IsOpenLoot{};
        bool IsLootTaken{};
        bool IsDoor{};
        bool IsDoorStateKnown{};
        bool IsDoorOpen{};
        bool IsContainer{};
        bool IsLocked{};
        uint8_t LockLevel{};
        Vector<ObjectStateDigest::ItemCount> Items{};
    };

    struct Mismatch
    {
        Field Kind{};
        std::string Server{};
        std::string Client{};
    };

    static const char* FieldName(Field aField) noexcept
    {
        switch (aField)
        {
        case Field::kHarvested: return "harvested";
        case Field::kLootTaken: return "taken";
        case Field::kLock: return "lock";
        case Field::kDoor: return "door";
        case Field::kInventory: return "inventory";
        }
        return "?";
    }

    static std::string FormatItems(const Vector<ObjectStateDigest::ItemCount>& acItems)
    {
        std::string text = "{";
        for (std::size_t i = 0; i < acItems.size(); ++i)
        {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%s%X:%X x%d", i ? ", " : "", acItems[i].BaseId.ModId, acItems[i].BaseId.BaseId, acItems[i].Count);
            text += buffer;
        }
        return text + "}";
    }

    static std::string FormatLock(bool aIsLocked, uint8_t aLevel)
    {
        return aIsLocked ? "locked(" + std::to_string(aLevel) + ")" : "unlocked";
    }

    static std::vector<Mismatch> Compare(const ServerView& acServer, const ObjectStateDigest& acClient)
    {
        std::vector<Mismatch> result;
        const bool cClientDisabled = acClient.Has(ObjectStateDigest::kDisabled);

        if (acServer.IsHarvestable && acServer.IsHarvested != cClientDisabled)
            result.push_back({Field::kHarvested, acServer.IsHarvested ? "yes" : "no", cClientDisabled ? "yes" : "no"});

        if (acServer.IsOpenLoot && acServer.IsLootTaken != cClientDisabled)
            result.push_back({Field::kLootTaken, acServer.IsLootTaken ? "yes" : "no", cClientDisabled ? "yes" : "no"});

        // Lock data is sent to clients only for trusted records; untrusted ones stay client-owned.
        if (acServer.HasTrustedState)
        {
            const bool cClientLocked = acClient.Has(ObjectStateDigest::kLocked);
            if (acServer.IsLocked != cClientLocked || (cClientLocked && acServer.LockLevel != acClient.LockLevel))
                result.push_back({Field::kLock, FormatLock(acServer.IsLocked, acServer.LockLevel), FormatLock(cClientLocked, acClient.LockLevel)});
        }

        if (acServer.IsDoor && acServer.IsDoorStateKnown)
        {
            const bool cClientOpen = acClient.Has(ObjectStateDigest::kDoorOpen);
            if (acServer.IsDoorOpen != cClientOpen)
                result.push_back({Field::kDoor, acServer.IsDoorOpen ? "open" : "closed", cClientOpen ? "open" : "closed"});
        }

        if (acServer.IsContainer && acServer.HasTrustedState && acClient.Has(ObjectStateDigest::kHasInventory) && !(acServer.Items == acClient.Items))
            result.push_back({Field::kInventory, FormatItems(acServer.Items), FormatItems(acClient.Items)});

        return result;
    }

    // One report can land mid-transition (a door swinging, a transfer in flight).
    // A mismatch is logged only when the same one is seen in consecutive reports,
    // once per distinct value, and its end is logged as resolved.
    class Tracker
    {
    public:
        static constexpr uint32_t kConfirmReports = 2;

        enum class Event : uint8_t
        {
            kNone,
            kNew,
            kResolved,
        };

        struct Key
        {
            uint32_t PlayerId{};
            GameId Id{};
            Field Kind{};

            bool operator==(const Key& acRhs) const noexcept { return PlayerId == acRhs.PlayerId && Id == acRhs.Id && Kind == acRhs.Kind; }
        };

        struct KeyHash
        {
            std::size_t operator()(const Key& acKey) const noexcept
            {
                return std::hash<GameId>()(acKey.Id) ^ (std::hash<uint32_t>()(acKey.PlayerId) << 3) ^ static_cast<std::size_t>(acKey.Kind);
            }
        };

        // aSignature is empty when the field matches.
        Event Observe(const Key& acKey, const std::string& acSignature)
        {
            auto it = m_entries.find(acKey);
            if (acSignature.empty())
            {
                if (it == m_entries.end())
                    return Event::kNone;
                const bool cWasLogged = it->second.IsLogged;
                m_entries.erase(it);
                return cWasLogged ? Event::kResolved : Event::kNone;
            }

            if (it == m_entries.end())
                it = m_entries.emplace(acKey, Entry{}).first;

            Entry& entry = it->second;
            if (entry.Signature != acSignature)
            {
                entry.Signature = acSignature;
                entry.Seen = 0;
                entry.IsLogged = false;
            }

            ++entry.Seen;
            if (!entry.IsLogged && entry.Seen >= kConfirmReports)
            {
                entry.IsLogged = true;
                return Event::kNew;
            }
            return Event::kNone;
        }

        void ForgetPlayer(uint32_t aPlayerId)
        {
            for (auto it = m_entries.begin(); it != m_entries.end();)
                it = it->first.PlayerId == aPlayerId ? m_entries.erase(it) : std::next(it);
        }

        std::size_t Size() const noexcept { return m_entries.size(); }

    private:
        struct Entry
        {
            std::string Signature{};
            uint32_t Seen{};
            bool IsLogged{};
        };

        std::unordered_map<Key, Entry, KeyHash> m_entries;
    };
};
