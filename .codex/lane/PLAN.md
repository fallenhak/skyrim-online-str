# Population / ESLoader Parallel Lane

Branch: parallel/population-loader
Issue: #33
Base: 148021a9517bc77b53aa484f1b97b53192feff07

## Ownership boundary
Primary areas: Code/components/es_loader, server ModsComponent/form-id resolver/population policy/identity resolver and ActorPopulationTests/docs.
Do NOT modify combat, progression, inventory/magic/object interaction, or UI.

## Queue
L01 Independently review current opt-in actor record loader and identity resolver for real-modlist correctness.
L02 Fix loadorder.txt parsing hazards: CR/LF, whitespace, duplicate lines, missing files, deterministic ordering, filename normalization. Preserve metadata-only default behavior.
L03 Research TES4 plugin flags in loader. Correctly identify ESL/light namespace including ESL-flagged .esp if current extension heuristic is insufficient. Use plugin header authority, not client claim.
L04 Harden standard/light ID assignment and overflow/bounds; detect too many standard/light plugins cleanly.
L05 Verify master mapping/reference prefix resolution for NPC, RACE, ACHR across multiple masters and overrides.
L06 Verify override precedence when later plugins override master-defined ACHR/NPC/RACE records.
L07 Missing master / unresolved form behavior must remain explicit Unknown, never guessed.
L08 Harden chunk/record bounds for minimal NPC/RACE/ACHR parsing against truncated/malformed plugin data without crashing.
L09 Filename case/path normalization and mapping between network ModsComponent names and server load metadata; avoid CR/path mismatches.
L10 Data directory/loadorder absence and partial modlist diagnostics; no forced record loading.
L11 Add synthetic tests for standard plugins, true light plugins, ESL-flagged ESP, overrides, malformed records, duplicate/missing masters, unknown network IDs and spoofed prefix bits.
L12 Assess performance/memory of opt-in full record parsing; implement low-risk bounded/indexing improvements only if measurable from code.
L13 Expand conservative population policy configurability without speculating edge races. Preserve 10 normal humanoid defaults and Unknown for unsupported classes.
L14 Verify humanoid assignment gate works correctly with hardened loader metadata and no-record default.
L15 Cross-platform GCC/MSVC/path portability pass.
L16 Final independent parser/security review and focused tests.

## Hard boundaries
Record loading remains opt-in by default unless architecture evidence justifies a separate reviewed change. Never classify Unknown as Creature/Humanoid. No combat/reward/UI work.
