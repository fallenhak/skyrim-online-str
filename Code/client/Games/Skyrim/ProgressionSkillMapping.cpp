#include <ProgressionSkillMapping.h>

#include <Forms/ActorValueInfo.h>

std::optional<ProgressionSkill> ProgressionSkillFromActorValue(const std::int32_t aActorValue) noexcept
{
    switch (aActorValue)
    {
    case ActorValueInfo::kOneHanded: return ProgressionSkill::kOneHanded;
    case ActorValueInfo::kTwoHanded: return ProgressionSkill::kTwoHanded;
    case ActorValueInfo::kMarksman: return ProgressionSkill::kArchery;
    case ActorValueInfo::kBlock: return ProgressionSkill::kBlock;
    case ActorValueInfo::kSmithing: return ProgressionSkill::kSmithing;
    case ActorValueInfo::kHeavyArmor: return ProgressionSkill::kHeavyArmor;
    case ActorValueInfo::kLightArmor: return ProgressionSkill::kLightArmor;
    case ActorValueInfo::kPickpocket: return ProgressionSkill::kPickpocket;
    case ActorValueInfo::kLockpicking: return ProgressionSkill::kLockpicking;
    case ActorValueInfo::kSneak: return ProgressionSkill::kSneak;
    case ActorValueInfo::kAlchemy: return ProgressionSkill::kAlchemy;
    case ActorValueInfo::kSpeechcraft: return ProgressionSkill::kSpeech;
    case ActorValueInfo::kAlteration: return ProgressionSkill::kAlteration;
    case ActorValueInfo::kConjuration: return ProgressionSkill::kConjuration;
    case ActorValueInfo::kDestruction: return ProgressionSkill::kDestruction;
    case ActorValueInfo::kIllusion: return ProgressionSkill::kIllusion;
    case ActorValueInfo::kRestoration: return ProgressionSkill::kRestoration;
    case ActorValueInfo::kEnchanting: return ProgressionSkill::kEnchanting;
    default: return std::nullopt;
    }
}

std::optional<std::int32_t> ActorValueFromProgressionSkill(const ProgressionSkill aSkill) noexcept
{
    switch (aSkill)
    {
    case ProgressionSkill::kOneHanded: return ActorValueInfo::kOneHanded;
    case ProgressionSkill::kTwoHanded: return ActorValueInfo::kTwoHanded;
    case ProgressionSkill::kArchery: return ActorValueInfo::kMarksman;
    case ProgressionSkill::kBlock: return ActorValueInfo::kBlock;
    case ProgressionSkill::kSmithing: return ActorValueInfo::kSmithing;
    case ProgressionSkill::kHeavyArmor: return ActorValueInfo::kHeavyArmor;
    case ProgressionSkill::kLightArmor: return ActorValueInfo::kLightArmor;
    case ProgressionSkill::kPickpocket: return ActorValueInfo::kPickpocket;
    case ProgressionSkill::kLockpicking: return ActorValueInfo::kLockpicking;
    case ProgressionSkill::kSneak: return ActorValueInfo::kSneak;
    case ProgressionSkill::kAlchemy: return ActorValueInfo::kAlchemy;
    case ProgressionSkill::kSpeech: return ActorValueInfo::kSpeechcraft;
    case ProgressionSkill::kAlteration: return ActorValueInfo::kAlteration;
    case ProgressionSkill::kConjuration: return ActorValueInfo::kConjuration;
    case ProgressionSkill::kDestruction: return ActorValueInfo::kDestruction;
    case ProgressionSkill::kIllusion: return ActorValueInfo::kIllusion;
    case ProgressionSkill::kRestoration: return ActorValueInfo::kRestoration;
    case ProgressionSkill::kEnchanting: return ActorValueInfo::kEnchanting;
    default: return std::nullopt;
    }
}
