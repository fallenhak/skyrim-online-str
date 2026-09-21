param(
    [double]$Hours = 5.5,
    [string]$Model = "gpt-5.6",
    [ValidateSet("low","medium","high","xhigh","max")]
    [string]$Effort = "high",
    [int]$FailureRetrySeconds = 300
)

$ErrorActionPreference = "Stop"
$ExpectedBranch = "hardening/longhaul-creature-combat"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments=$true)][string[]]$GitArgs)
    & git @GitArgs
    if ($LASTEXITCODE -ne 0) {
        throw "git $($GitArgs -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Get-DirtyState {
    return ((& git status --porcelain) -join [Environment]::NewLine)
}

function Push-Head {
    $aheadText = ((& git rev-list --count "origin/$ExpectedBranch..HEAD") -join "").Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to calculate commits ahead of origin/$ExpectedBranch"
    }

    $ahead = 0
    if (-not [int]::TryParse($aheadText, [ref]$ahead)) {
        throw "Unexpected rev-list output: $aheadText"
    }

    if ($ahead -gt 0) {
        Write-Host "Pushing $ahead commit(s) to origin/$ExpectedBranch..."
        Invoke-Git push origin "HEAD:$ExpectedBranch"
    }
}

if (-not (Get-Command codex -ErrorAction SilentlyContinue)) {
    throw "codex CLI was not found in PATH."
}

$branch = ((& git branch --show-current) -join "").Trim()
if ($branch -ne $ExpectedBranch) {
    throw "Wrong branch '$branch'. Expected '$ExpectedBranch'."
}

Write-Host "Codex version:"
& codex --version

if (Get-DirtyState) {
    throw "Worktree is dirty before long-haul start. Commit or stash your own work first."
}

Invoke-Git fetch origin
Invoke-Git pull --ff-only origin $ExpectedBranch

$Deadline = (Get-Date).AddHours($Hours)
$Round = 0
$ConsecutiveFailures = 0
$LogRoot = Join-Path $env:TEMP "skyrim-online-str-codex-longhaul"
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
$BasePrompt = Get-Content ".codex/longhaul/ITERATION_PROMPT.md" -Raw

Write-Host ""
Write-Host "Long-haul Codex loop started."
Write-Host "Branch: $ExpectedBranch"
Write-Host "Model: $Model"
Write-Host "Effort: $Effort"
Write-Host "Deadline: $Deadline"
Write-Host "Logs: $LogRoot"
Write-Host ""

while ((Get-Date) -lt $Deadline) {
    $Round++
    $Remaining = $Deadline - (Get-Date)
    $LogFile = Join-Path $LogRoot ("round-{0:D3}-{1}.log" -f $Round, (Get-Date -Format "yyyyMMdd-HHmmss"))

    Write-Host "============================================================"
    Write-Host "ROUND $Round | remaining $([math]::Round($Remaining.TotalHours, 2))h"
    Write-Host "============================================================"

    $dirtyBefore = Get-DirtyState

    if (-not $dirtyBefore) {
        try {
            Invoke-Git fetch origin
            Push-Head
            Invoke-Git pull --ff-only origin $ExpectedBranch
        }
        catch {
            Write-Warning "Git sync failed: $($_.Exception.Message)"
            Start-Sleep -Seconds 60
            continue
        }
    }

    $BeforeHead = ((& git rev-parse HEAD) -join "").Trim()

    if ($dirtyBefore) {
        $RunPrompt = @"
A previous automated Codex iteration exited with uncommitted work.
Do NOT start a new phase yet.

Read .codex/longhaul/PLAN.md and .codex/longhaul/STATE.md.
Inspect the current diff and recent history. The dirty changes belong to the autonomous long-haul loop.

Recover safely:
- determine the intended in-progress phase;
- finish it if safe, including tests and STATE.md;
- if partial changes are wrong, edit them into a safe final form rather than blindly resetting;
- never discard earlier committed work;
- never rebase, merge, force-push, or push;
- commit all completed work;
- leave git status clean.

If the partial phase cannot be completed safely, document the blocker in STATE.md, undo only its uncommitted partial changes with targeted edits, commit the blocker/state if appropriate, and leave the worktree clean.

Then exit.
"@
    }
    else {
        $RunPrompt = $BasePrompt + @"

Supervisor metadata:
iteration: $Round
wall-clock deadline: $($Deadline.ToString("o"))
approximate time remaining: $([math]::Round($Remaining.TotalMinutes)) minutes

Complete one substantial phase cleanly and exit. The supervisor will launch a fresh context immediately.
"@
    }

    $ReasoningConfig = 'model_reasoning_effort="' + $Effort + '"'
    $CodexArgs = @(
        "exec",
        "--sandbox", "workspace-write",
        "--ask-for-approval", "never",
        "--model", $Model,
        "-c", $ReasoningConfig,
        $RunPrompt
    )

    Write-Host "Launching fresh Codex exec..."
    & codex @CodexArgs 2>&1 | Tee-Object -FilePath $LogFile
    $CodexExit = $LASTEXITCODE

    if ($CodexExit -ne 0) {
        $ConsecutiveFailures++
        Write-Warning "Codex exited with code $CodexExit. Failure count: $ConsecutiveFailures"

        if ((Get-Date) -ge $Deadline) {
            break
        }

        $sleep = $FailureRetrySeconds
        if ($ConsecutiveFailures -ge 3) {
            $sleep = [Math]::Max($FailureRetrySeconds, 600)
        }

        Write-Host "Retrying after $sleep seconds."
        Start-Sleep -Seconds $sleep
        continue
    }

    $ConsecutiveFailures = 0
    $dirtyAfter = Get-DirtyState

    if ($dirtyAfter) {
        Write-Warning "Codex exited successfully but left a dirty worktree."
        Write-Host "Next round will enter recovery mode."
        Start-Sleep -Seconds 10
        continue
    }

    $AfterHead = ((& git rev-parse HEAD) -join "").Trim()

    if ($AfterHead -eq $BeforeHead) {
        Write-Warning "No commit was produced in round $Round."
    }
    else {
        Write-Host "Round $Round committed: $AfterHead"
    }

    try {
        Push-Head
    }
    catch {
        Write-Warning "Push failed. Next clean round will retry: $($_.Exception.Message)"
    }

    if ((Get-Date) -lt $Deadline) {
        Start-Sleep -Seconds 10
    }
}

Write-Host ""
Write-Host "Deadline reached. Performing final repository hygiene."

if (Get-DirtyState) {
    $CleanupPrompt = @"
The long-haul supervisor reached its wall-clock deadline and the repository is dirty from the last autonomous iteration.

Do not start new work.
Read the diff and .codex/longhaul/STATE.md.
Safely finish the in-progress phase if close and supported, or reduce it to a safe documented state using targeted edits.
Run narrow relevant tests and git diff --check.
Commit retained changes.
Do not merge, rebase, force-push, or push.
Leave git status clean, then exit.
"@

    $ReasoningConfig = 'model_reasoning_effort="' + $Effort + '"'
    $CleanupArgs = @(
        "exec",
        "--sandbox", "workspace-write",
        "--ask-for-approval", "never",
        "--model", $Model,
        "-c", $ReasoningConfig,
        $CleanupPrompt
    )

    & codex @CleanupArgs 2>&1 | Tee-Object -FilePath (Join-Path $LogRoot "final-cleanup.log")
}

if (-not (Get-DirtyState)) {
    try {
        Push-Head
    }
    catch {
        Write-Warning "Final push failed: $($_.Exception.Message)"
    }
}
else {
    Write-Warning "FINAL WARNING: worktree is still dirty. Inspect before other work."
}

Write-Host ""
Write-Host "Long-haul loop finished."
Write-Host "Final HEAD: $(((& git rev-parse HEAD) -join '').Trim())"
Write-Host "Status:"
& git status --short
Write-Host "Logs: $LogRoot"
