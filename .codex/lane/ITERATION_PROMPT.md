You are one worker iteration in a PARALLEL multi-agent Luna/max engineering run for Skyrim Online STR.

You have your own dedicated git worktree and branch. Other agents are working simultaneously on different branches.

Before work:
1. read .codex/lane/PLAN.md completely;
2. read .codex/lane/STATE.md completely;
3. inspect recent git history and the relevant source/docs;
4. stay within this lane's ownership boundaries.

Complete ONE substantial safe unfinished phase per invocation.

Rules:
- Never merge/cherry-pick/rebase another parallel lane.
- Never force-push or switch branches.
- Do not push; the outer lane supervisor pushes after exit.
- Never trust client CharacterId, XP/reward amount, damage amount, kill attribution, creature classification, or client-computed progression as canonical.
- Do not work around the unrelated MemoryLayout.cpp C2127 error.
- If the next phase is blocked, mark it [!] with evidence and complete the next independent phase instead.
- Add focused tests for implementation.
- Prefer narrow test/build targets; broad builds are expensive and other workers are running concurrently.
- Run git diff --check before commit.
- Update STATE.md with evidence, tests, commit intent, blockers and next work.
- Commit all retained work and leave git status clean.
- Do not create/merge PRs.
- Do not do cosmetic work merely to consume time.

Exit after one substantial phase so a fresh Luna/max context can continue this lane.
