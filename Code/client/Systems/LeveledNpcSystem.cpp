#include <TiltedOnlinePCH.h>

#include <Systems/LeveledNpcSystem.h>
#include <Actor.h>
#include <ExtraData/ExtraLeveledCreature.h>
#include <Forms/TESNPC.h>
#include <Misc/GarbageCollector.h>

bool LeveledNpcSystem::IsLeveledNpcBase(const TESNPC* apBase) noexcept
{
    if (!apBase || apBase->IsTemporary())
        return false;

    const TESForm* pTemplate = apBase->actorData.baseTemplateForm;
    return pTemplate && pTemplate->formType == FormType::LeveledCharacter;
}

TESNPC* LeveledNpcSystem::GetOriginalBase(const Actor* apActor) noexcept
{
    if (!apActor)
        return nullptr;

    const auto* pExtra = static_cast<ExtraLeveledCreature*>(apActor->extraData.GetByType(ExtraDataType::LeveledCreature));
    if (pExtra && pExtra->originalBase)
        return Cast<TESNPC>(pExtra->originalBase);

    auto* pBase = Cast<TESNPC>(apActor->baseForm);
    return IsLeveledNpcBase(pBase) ? pBase : nullptr;
}

bool LeveledNpcSystem::ApplyPick(Actor* apActor, TESNPC* apPick) noexcept
{
    if (!apPick)
        return false;

    // Skyrim resolves a leveled NPC by copying the original base, then
    // applying the pick according to that base's template flags. Using
    // the pick itself discards data such as a hold guard's name/outfit.
    auto* pOriginalBase = GetOriginalBase(apActor);
    auto* pResolvedBase = pOriginalBase ? TESActorBaseData::CreateTemplateActorBase(pOriginalBase, apPick) : nullptr;
    if (!pResolvedBase)
        return false;

    auto* pOldBase = Cast<TESNPC>(apActor->baseForm);
    apActor->SetLeveledCreature(pOriginalBase, apPick);
    apActor->SetObjectReference(pResolvedBase);

    // Match RecalcLeveledActor's disposal policy, but never dispose of
    // a static pick left by the old reconciliation implementation.
    if (pOldBase && pOldBase->IsTemporary() && pOldBase != pOriginalBase && pOldBase != apPick)
        GarbageCollector::Get()->Add(pOldBase);

    spdlog::info("Applied leveled NPC pick for actor {:X}, original base: {:X}, base: {:X}, pick: {:X}",
        apActor->formID, pOriginalBase->formID, pResolvedBase->formID, apPick->formID);
    return true;
}

bool LeveledNpcSystem::FixPlayerLevelScaling(TESNPC* apBase) noexcept
{
    // ACBS flag 0x80; level then holds the multiplier * 1000.
    constexpr uint32_t kPcLevelMult = 1 << 7;
    // Same as the server's PluginContainerContents::kDefaultPlaceLevel.
    constexpr uint32_t kPlaceLevel = 10;

    if (!apBase || !(apBase->actorData.actorBaseFlags & kPcLevelMult))
        return false;

    auto& data = apBase->actorData;
    uint32_t level = (static_cast<uint32_t>(data.level) * kPlaceLevel + 500) / 1000;
    level = std::max<uint32_t>(level, std::max<uint32_t>(1, data.calcLevelMin));
    if (data.calcLevelMax != 0)
        level = std::min<uint32_t>(level, std::max<uint32_t>(data.calcLevelMax, data.calcLevelMin));

    spdlog::info("Fixed player-scaled level of NPC base {:X}: multiplier {:.2f} -> level {}", apBase->formID, data.level / 1000.f, level);
    data.level = static_cast<uint16_t>(level);
    data.actorBaseFlags &= ~kPcLevelMult;
    return true;
}
