#pragma once

#include <Persistence/CharacterRecord.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <vector>

/**
 * A server-side identity for one target incarnation.
 *
 * Server entity IDs can be reused after an ECS entity is removed, so a target
 * ID alone is not sufficient for a future combat contribution record.
 */
struct CombatContributionTarget final
{
    std::uint32_t ServerId{};
    std::uint64_t LifecycleGeneration{};

    friend bool operator<(const CombatContributionTarget& acLeft, const CombatContributionTarget& acRight) noexcept
    {
        return std::tie(acLeft.ServerId, acLeft.LifecycleGeneration) < std::tie(acRight.ServerId, acRight.LifecycleGeneration);
    }

    friend bool operator==(const CombatContributionTarget& acLeft, const CombatContributionTarget& acRight) noexcept
    {
        return acLeft.ServerId == acRight.ServerId && acLeft.LifecycleGeneration == acRight.LifecycleGeneration;
    }
};

struct CombatContribution final
{
    Persistence::CharacterId AttackerCharacterId{};
    std::uint64_t LastObservedTick{};
    std::uint32_t ObservationCount{};
};

/**
 * Bounded, deterministic storage for validated combat observations.
 *
 * The ledger intentionally has no packet or Player API. A future server
 * handler must resolve sender ownership and the persistent character ID before
 * calling RecordValidatedContribution.
 */
class CombatContributionLedger final
{
public:
    using Target = CombatContributionTarget;

    explicit CombatContributionLedger(const std::uint64_t aExpiryTicks = 30 * 60,
                                      const std::size_t aMaxTargets = 1024,
                                      const std::size_t aMaxContributorsPerTarget = 16) noexcept
        : m_expiryTicks(aExpiryTicks)
        , m_maxTargets(aMaxTargets)
        , m_maxContributorsPerTarget(aMaxContributorsPerTarget)
    {
    }

    [[nodiscard]] bool RecordValidatedContribution(const Target aTarget, const Persistence::CharacterId aAttackerCharacterId,
                                                   const std::uint64_t aObservedTick)
    {
        if (!IsValidTarget(aTarget) || aAttackerCharacterId <= 0)
            return false;

        ExpireOld(aObservedTick);

        auto targetIt = m_contributions.find(aTarget);
        if (targetIt == m_contributions.end())
        {
            if (m_contributions.size() >= m_maxTargets)
                return false;

            targetIt = m_contributions.emplace(aTarget, ContributorMap{}).first;
        }

        auto& contributors = targetIt->second;
        const auto contributorIt = contributors.find(aAttackerCharacterId);
        if (contributorIt != contributors.end())
        {
            auto& contribution = contributorIt->second;
            contribution.LastObservedTick = aObservedTick;
            if (contribution.ObservationCount != std::numeric_limits<std::uint32_t>::max())
                ++contribution.ObservationCount;
            return true;
        }

        if (contributors.size() >= m_maxContributorsPerTarget)
            return false;

        contributors.emplace(aAttackerCharacterId, CombatContribution{aAttackerCharacterId, aObservedTick, 1});
        return true;
    }

    /**
     * Return all live contributors in CharacterId order and erase the target.
     * A second consume therefore returns an empty result.
     */
    [[nodiscard]] std::vector<CombatContribution> ConsumeContributionsForDeath(const Target aTarget, const std::uint64_t aNow)
    {
        ExpireOld(aNow);

        const auto targetIt = m_contributions.find(aTarget);
        if (targetIt == m_contributions.end())
            return {};

        std::vector<CombatContribution> result;
        result.reserve(targetIt->second.size());
        for (const auto& [attackerCharacterId, contribution] : targetIt->second)
        {
            (void)attackerCharacterId;
            result.push_back(contribution);
        }

        m_contributions.erase(targetIt);
        return result;
    }

    /**
     * Return only the persistent character identities resolved into the
     * ledger, in deterministic CharacterId order, then erase the target.
     */
    [[nodiscard]] std::vector<Persistence::CharacterId> ConsumeCharacterIdsForDeath(const Target aTarget, const std::uint64_t aNow)
    {
        const auto contributions = ConsumeContributionsForDeath(aTarget, aNow);
        std::vector<Persistence::CharacterId> characterIds;
        characterIds.reserve(contributions.size());
        for (const auto& contribution : contributions)
            characterIds.push_back(contribution.AttackerCharacterId);
        return characterIds;
    }

    void ClearTarget(const Target aTarget) noexcept
    {
        m_contributions.erase(aTarget);
    }

    /** Remove all target contribution records for every lifecycle of an actor. */
    void ClearEntity(const std::uint32_t aServerId) noexcept
    {
        if (aServerId == std::numeric_limits<std::uint32_t>::max())
            return;

        auto targetIt = m_contributions.lower_bound(Target{aServerId, 0});
        while (targetIt != m_contributions.end() && targetIt->first.ServerId == aServerId)
            targetIt = m_contributions.erase(targetIt);
    }

    /** Remove a persistent attacker without identifying it by connection. */
    void RemoveCharacter(const Persistence::CharacterId aAttackerCharacterId) noexcept
    {
        if (aAttackerCharacterId <= 0)
            return;

        for (auto targetIt = m_contributions.begin(); targetIt != m_contributions.end();)
        {
            targetIt->second.erase(aAttackerCharacterId);
            if (targetIt->second.empty())
                targetIt = m_contributions.erase(targetIt);
            else
                ++targetIt;
        }
    }

    void ExpireOld(const std::uint64_t aNow) noexcept
    {
        for (auto targetIt = m_contributions.begin(); targetIt != m_contributions.end();)
        {
            auto& contributors = targetIt->second;
            for (auto contributorIt = contributors.begin(); contributorIt != contributors.end();)
            {
                const auto lastObservedTick = contributorIt->second.LastObservedTick;
                const bool expired = aNow > lastObservedTick && aNow - lastObservedTick > m_expiryTicks;
                if (expired)
                    contributorIt = contributors.erase(contributorIt);
                else
                    ++contributorIt;
            }

            if (contributors.empty())
                targetIt = m_contributions.erase(targetIt);
            else
                ++targetIt;
        }
    }

    [[nodiscard]] std::size_t TargetCount() const noexcept
    {
        return m_contributions.size();
    }

    [[nodiscard]] std::size_t ContributionCount() const noexcept
    {
        std::size_t count = 0;
        for (const auto& [target, contributors] : m_contributions)
        {
            (void)target;
            count += contributors.size();
        }
        return count;
    }

private:
    using ContributorMap = std::map<Persistence::CharacterId, CombatContribution>;
    using ContributionMap = std::map<Target, ContributorMap>;

    [[nodiscard]] static bool IsValidTarget(const Target aTarget) noexcept
    {
        return aTarget.ServerId != std::numeric_limits<std::uint32_t>::max() && aTarget.LifecycleGeneration != 0;
    }

    std::uint64_t m_expiryTicks;
    std::size_t m_maxTargets;
    std::size_t m_maxContributorsPerTarget;
    ContributionMap m_contributions;
};
