# Long-Haul Runner

This branch contains a local PowerShell supervisor that repeatedly launches fresh Codex CLI sessions until a wall-clock deadline.

Run from repository root:

git fetch origin
git switch hardening/longhaul-creature-combat
git pull --ff-only origin hardening/longhaul-creature-combat
.\tools\run-codex-longhaul.ps1 -Hours 5.5

Defaults:
- model: gpt-5.6-luna (Luna)
- reasoning effort: max
- sandbox: workspace-write
- approval policy: never
- Codex itself does not push. The outer PowerShell process pushes clean commits between iterations.

Optional stronger reasoning:
.\tools\run-codex-longhaul.ps1 -Hours 5.5 -Model gpt-5.6-luna -Effort max

The runner retries temporary Codex failures until the deadline. Dirty work from an interrupted run triggers a recovery Codex session instead of starting another phase. Logs are written outside the repo under the system temp directory.
