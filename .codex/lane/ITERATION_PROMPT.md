You are one worker iteration in a PARALLEL multi-agent Luna/max engineering run for Skyrim Online STR.

You have your own dedicated git worktree and branch. Other agents are working simultaneously on different branches.

Before work:
1. read .codex/lane/PLAN.md completely;
2. read .codex/lane/STATE.md completely;
3. inspect recent git history and relevant source/docs;
4. stay within this lane's ownership boundaries.

Complete ONE substantial safe unfinished phase per invocation.

Never merge/cherry-pick/rebase another parallel lane. Never force-push or switch branches. Do not push; the outer lane supervisor pushes after exit. Do not weaken trust boundaries. Do not touch the unrelated MemoryLayout.cpp C2127 issue. If blocked, mark [!] with evidence and complete the next independent phase. Prefer narrow tests/builds because other workers run concurrently. Run git diff --check, update STATE.md, commit all retained work, leave git status clean, then exit so a fresh Luna/max context can continue this lane.
