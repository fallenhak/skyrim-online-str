You are one iteration of a multi-process long-haul engineering loop for Skyrim Online STR.

Repository branch MUST remain hardening/longhaul-creature-combat.

Before doing anything:
1. read .codex/longhaul/PLAN.md completely;
2. read .codex/longhaul/STATE.md completely;
3. read relevant architecture docs and source for the highest-priority unfinished phase;
4. inspect recent git history so completed work is not repeated.

Complete ONE substantial safe phase in this invocation.

Rules:
- Prefer highest-priority unfinished phase.
- If research proves it is genuinely blocked by an unresolved authority decision, mark it [!] with evidence in STATE.md, then select the next independent phase and complete that instead. Do not exit merely because one phase is blocked.
- Do not weaken trust boundaries to make progress.
- Never make client CharacterId, XP, reward amount, damage amount, creature class, skill/level result, or kill attribution canonical.
- Never merge, rebase, force-push, or change branch.
- Do not push; the external supervisor pushes after exit.
- Do not touch the unrelated MemoryLayout.cpp C2127 problem.
- Use subagents when they materially help broad audits/reviews, but ground final decisions in source.
- Research actual semantics before protocol changes.
- Add focused tests for implementation.
- Run git diff --check before committing.
- Update STATE.md with evidence, changes, tests and remaining limitations.
- Update architecture docs when behavior or authority boundaries change.
- Create focused git commit(s).
- Before exiting, git status MUST be clean.
- If the phase is already satisfied, perform genuine independent verification and add only concrete-value tests/docs, mark complete, commit evidence and exit.
- No cosmetic refactors just to create work.
- Do not create or merge a PR.

A successful iteration ends only after one meaningful phase is completed or a concrete correctness/security problem is fixed, tests are green or unrelated known failures documented, STATE.md is updated, changes are committed, and worktree is clean.

Then exit normally. The supervisor will immediately start a fresh Codex context for the next phase.
