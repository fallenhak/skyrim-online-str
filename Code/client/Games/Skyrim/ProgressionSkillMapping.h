#pragma once

#include <Structs/Progression.h>

#include <cstdint>
#include <optional>

[[nodiscard]] std::optional<ProgressionSkill> ProgressionSkillFromActorValue(std::int32_t aActorValue) noexcept;
[[nodiscard]] std::optional<std::int32_t> ActorValueFromProgressionSkill(ProgressionSkill aSkill) noexcept;
