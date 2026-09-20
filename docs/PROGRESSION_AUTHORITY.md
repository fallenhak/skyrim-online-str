# Progression authority

The progression boundary is intentionally narrow in this milestone.

## Authority model

- Offline/disconnected play keeps vanilla Skyrim skill progression.
- Once the client connection is accepted, local skill-XP application is blocked. The client may emit a skill-use intent for future server-side producers, but it does not send an XP amount, character id, or reward claim.
- A future server producer will validate a reward and issue `NotifyProgressionAward` to one player. The award contains a runtime `AwardId`, the server-owned persistent `CharacterId`, a stable `ProgressionSkill`, an XP amount, and a `ProgressionAwardReason`.
- The client accepts awards only while its character session is `InWorld`, for the active persistent character, with a valid finite positive amount and skill. Recent award ids are deduplicated before the trusted Skyrim `PlayerCharacter::AddSkillExperience` wrapper is called.

The current client-only skill-use intent event has no gameplay consumer. No kill, quest, or script producer is implemented yet, and no client request can create an award.

## Frozen limitations

Level is not client-authoritative for persistent characters. This milestone adds no progression level write-back or skill-state load path; the existing character snapshot level remains unchanged. Character persistence therefore still has no skill schema, XP schema, or perk schema. Bootstrap skill state remains non-canonical until a later persistence milestone defines it.

The old `SyncExperienceRequest` / `NotifySyncExperience` protocol entries remain only as deprecated compatibility placeholders so opcode numbering is stable. Their active service paths and the `Gameplay:bEnableXpSync` setting have been removed.
