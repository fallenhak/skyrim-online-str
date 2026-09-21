# Parallel Luna/max overnight workers

This setup runs multiple real coding agents concurrently. There is no controller/reviewer agent in the worker pool.

Each lane has:
- its own Git branch;
- its own Git worktree directory;
- its own task queue and persistent STATE.md;
- repeated fresh Codex CLI contexts;
- Luna with max reasoning by default.

Default lanes:
1. combat -> parallel/combat-foundations (#31)
2. authority -> parallel/interaction-authority (#32)
3. population -> parallel/population-loader (#33)
4. ui -> parallel/character-ui (#34)

Start from the main repository checkout on hardening/longhaul-creature-combat:

git fetch origin
git switch hardening/longhaul-creature-combat
git pull --ff-only origin hardening/longhaul-creature-combat
powershell -ExecutionPolicy Bypass -File .\tools\start-parallel-luna.ps1 -Hours 5.5

The launcher creates sibling worktrees under:
skyrim-online-str-workers\combat
skyrim-online-str-workers\authority
skyrim-online-str-workers\population
skyrim-online-str-workers\ui

Every lane commits and pushes only its own branch. Nothing is merged automatically.

To run fewer lanes:
powershell -ExecutionPolicy Bypass -File .\tools\start-parallel-luna.ps1 -Hours 5.5 -Lanes "combat,population,ui"

Parallel workers share the same Codex account quota and the same physical PC. Four Luna/max workers can consume usage quickly and concurrent builds can contend for CPU/RAM/disk. Lane prompts therefore prefer focused tests/builds during the run. Final integration should happen only after human/architecture review.
