param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$ExpectedBranch,
    [Parameter(Mandatory=$true)][string]$WorkerName,
    [double]$Hours = 5.5,
    [string]$Model = "gpt-5.6-luna",
    [ValidateSet("low","medium","high","xhigh","max")][string]$Effort = "max",
    [int]$FailureRetrySeconds = 300
)

$ErrorActionPreference = "Stop"
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
    & git fetch origin | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "git fetch origin failed"
    }

    $aheadText = ((& git rev-list --count "origin/$ExpectedBranch..HEAD") -join "").Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to calculate commits ahead of origin/$ExpectedBranch"
    }

    $ahead = 0
    if (-not [int]::TryParse($aheadText, [ref]$ahead)) {
        throw "Unexpected rev-list output: $aheadText"
    }

    if ($ahead -gt 0) {
        Write-Host "[$WorkerName] pushing $ahead commit(s)..."
        Invoke-Git push origin "HEAD:$ExpectedBranch"
    }
}

$CodexCommand = Get-Command codex -ErrorAction SilentlyContinue
if (-not $CodexCommand) {
    throw "codex CLI was not found in PATH."
}
$CodexExe = $CodexCommand.Source
if (-not $CodexExe) {
    $CodexExe = $CodexCommand.Path
}
Write-Host "[$WorkerName] codex command: $CodexExe"

$branch = ((& git branch --show-current) -join "").Trim()
if ($branch -ne $ExpectedBranch) {
    throw "[$WorkerName] wrong branch '$branch'; expected '$ExpectedBranch'."
}

if (Get-DirtyState) {
    throw "[$WorkerName] worktree is dirty before start."
}

Invoke-Git fetch origin
Invoke-Git pull --ff-only origin $ExpectedBranch

$Deadline = (Get-Date).AddHours($Hours)
$Round = 0
$ConsecutiveFailures = 0
$NoCommitRounds = 0
$SafeWorkerName = ($WorkerName -replace '[^A-Za-z0-9_.-]', '_')
$LogRoot = Join-Path $env:TEMP ("skyrim-online-str-parallel-" + $SafeWorkerName)
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
$BasePrompt = Get-Content ".codex/lane/ITERATION_PROMPT.md" -Raw

Write-Host ""
Write-Host "[$WorkerName] lane started"
Write-Host "branch: $ExpectedBranch"
Write-Host "model: $Model"
Write-Host "effort: $Effort"
Write-Host "approval: never"
Write-Host "deadline: $Deadline"
Write-Host "logs: $LogRoot"
Write-Host ""

while ((Get-Date) -lt $Deadline) {
    $Round++
    $Remaining = $Deadline - (Get-Date)
    $LogFile = Join-Path $LogRoot ("round-{0:D3}-{1}.log" -f $Round, (Get-Date -Format "yyyyMMdd-HHmmss"))

    Write-Host "[$WorkerName] ROUND $Round | remaining $([math]::Round($Remaining.TotalHours, 2))h"

    $dirtyBefore = Get-DirtyState

    if (-not $dirtyBefore) {
        try {
            Push-Head
            Invoke-Git pull --ff-only origin $ExpectedBranch
        }
        catch {
            Write-Warning "[$WorkerName] git sync failed: $($_.Exception.Message)"
            Start-Sleep -Seconds 60
            continue
        }
    }

    Write-Host "[$WorkerName] git sync complete; resolving HEAD..."
    $BeforeHead = ((& git rev-parse HEAD) -join "").Trim()
    Write-Host "[$WorkerName] HEAD before round: $BeforeHead"

    if ($dirtyBefore) {
        $RunPrompt = @"
A previous automated iteration in this parallel worker exited with uncommitted work.
Do NOT begin another phase.

Read .codex/lane/PLAN.md and .codex/lane/STATE.md.
Inspect the dirty diff and recent history. These changes belong to this worker lane.

Recover safely:
- identify the in-progress phase;
- finish it if supported by source evidence;
- test it;
- update STATE.md;
- commit retained work;
- never reset --hard, merge, rebase, force-push, switch branches, or push;
- leave git status clean.

If the partial phase is unsafe to finish, document the blocker, undo only that phase's uncommitted edits with targeted changes, commit useful blocker/state documentation if warranted, and leave the worktree clean.
Then exit.
"@
    }
    else {
        $RunPrompt = $BasePrompt + @"

Supervisor metadata:
worker: $WorkerName
iteration: $Round
wall-clock deadline: $($Deadline.ToString("o"))
approximate remaining minutes: $([math]::Round($Remaining.TotalMinutes))
consecutive no-commit rounds before this run: $NoCommitRounds

Complete one substantial phase and exit cleanly. A fresh Luna/max context will continue this SAME lane immediately.

If your runtime header says sandbox: read-only, do not pretend implementation is possible. Exit without modifying state so the supervisor can retry after configuration is fixed.
"@
    }

    $ReasoningConfig = 'model_reasoning_effort="' + $Effort + '"'
    $CodexArgs = @(
        "exec",
        "--sandbox", "workspace-write",
        "--model", $Model,
        "-c", 'approval_policy="never"',
        "-c", $ReasoningConfig,
        $RunPrompt
    )

    Write-Host "[$WorkerName] launching Codex now (workspace-write, Luna/max)..."
    Write-Host "[$WorkerName] log: $LogFile"

    $PromptFile = Join-Path $LogRoot ("prompt-{0:D3}.txt" -f $Round)
    $StdoutFile = Join-Path $LogRoot ("stdout-{0:D3}.log" -f $Round)
    $StderrFile = Join-Path $LogRoot ("stderr-{0:D3}.log" -f $Round)
    Set-Content -LiteralPath $PromptFile -Value $RunPrompt -Encoding UTF8

    $CodexProcessArgs = @(
        "exec",
        "--sandbox", "workspace-write",
        "--model", $Model,
        "-c", 'approval_policy="never"',
        "-c", $ReasoningConfig,
        "-"
    )

    $proc = Start-Process -FilePath $CodexExe `
        -ArgumentList $CodexProcessArgs `
        -RedirectStandardInput $PromptFile `
        -RedirectStandardOutput $StdoutFile `
        -RedirectStandardError $StderrFile `
        -NoNewWindow `
        -PassThru

    Write-Host "[$WorkerName] Codex PID $($proc.Id) started."
    Write-Host "[$WorkerName] stdout: $StdoutFile"
    Write-Host "[$WorkerName] stderr/status: $StderrFile"

    while (-not $proc.HasExited) {
        $proc.Refresh()
        Start-Sleep -Seconds 15
    }

    $CodexExit = $proc.ExitCode

    if (Test-Path $StderrFile) {
        Get-Content $StderrFile | Add-Content -LiteralPath $LogFile
    }
    if (Test-Path $StdoutFile) {
        Get-Content $StdoutFile | Add-Content -LiteralPath $LogFile
    }

    Write-Host "[$WorkerName] Codex process returned exit code $CodexExit"

    if ($CodexExit -ne 0) {
        $ConsecutiveFailures++
        Write-Warning "[$WorkerName] Codex exit $CodexExit; failures: $ConsecutiveFailures"

        if ((Get-Date) -ge $Deadline) {
            break
        }

        $sleep = $FailureRetrySeconds
        if ($ConsecutiveFailures -ge 3) {
            $sleep = [Math]::Max($FailureRetrySeconds, 600)
        }
        Start-Sleep -Seconds $sleep
        continue
    }

    $ConsecutiveFailures = 0
    $dirtyAfter = Get-DirtyState

    if ($dirtyAfter) {
        Write-Warning "[$WorkerName] Codex left dirty worktree; next round enters recovery."
        Start-Sleep -Seconds 10
        continue
    }

    $AfterHead = ((& git rev-parse HEAD) -join "").Trim()
    if ($AfterHead -eq $BeforeHead) {
        $NoCommitRounds++
        Write-Warning "[$WorkerName] no commit in round $Round."
    }
    else {
        $NoCommitRounds = 0
        Write-Host "[$WorkerName] committed $AfterHead"
    }

    try {
        Push-Head
    }
    catch {
        Write-Warning "[$WorkerName] push failed; next round retries: $($_.Exception.Message)"
    }

    if ((Get-Date) -lt $Deadline) {
        Start-Sleep -Seconds 10
    }
}

Write-Host "[$WorkerName] deadline reached; final cleanup."

if (Get-DirtyState) {
    $CleanupPrompt = @"
This parallel worker has reached its wall-clock deadline with an uncommitted diff.
Do not start new work.
Read .codex/lane/STATE.md and the current diff.
Safely finish the current phase if close and supported, or reduce it to a safe documented state using targeted edits.
Run narrow relevant tests and git diff --check.
Commit retained work.
Do not merge, rebase, force-push, switch branches, or push.
Leave git status clean, then exit.
"@

    $ReasoningConfig = 'model_reasoning_effort="' + $Effort + '"'
    $CleanupArgs = @(
        "exec",
        "--sandbox", "workspace-write",
        "--model", $Model,
        "-c", 'approval_policy="never"',
        "-c", $ReasoningConfig,
        $CleanupPrompt
    )
    $CleanupLog = Join-Path $LogRoot "final-cleanup.log"
    $CleanupPromptFile = Join-Path $LogRoot "final-cleanup-prompt.txt"
    $CleanupStdout = Join-Path $LogRoot "final-cleanup-stdout.log"
    $CleanupStderr = Join-Path $LogRoot "final-cleanup-stderr.log"
    Set-Content -LiteralPath $CleanupPromptFile -Value $CleanupPrompt -Encoding UTF8

    $CleanupProcessArgs = @(
        "exec",
        "--sandbox", "workspace-write",
        "--model", $Model,
        "-c", 'approval_policy="never"',
        "-c", $ReasoningConfig,
        "-"
    )

    $cleanupProc = Start-Process -FilePath $CodexExe `
        -ArgumentList $CleanupProcessArgs `
        -RedirectStandardInput $CleanupPromptFile `
        -RedirectStandardOutput $CleanupStdout `
        -RedirectStandardError $CleanupStderr `
        -NoNewWindow `
        -PassThru `
        -Wait

    if (Test-Path $CleanupStderr) {
        Get-Content $CleanupStderr | Add-Content -LiteralPath $CleanupLog
    }
    if (Test-Path $CleanupStdout) {
        Get-Content $CleanupStdout | Add-Content -LiteralPath $CleanupLog
    }
}

if (-not (Get-DirtyState)) {
    try {
        Push-Head
    }
    catch {
        Write-Warning "[$WorkerName] final push failed: $($_.Exception.Message)"
    }
}
else {
    Write-Warning "[$WorkerName] FINAL WARNING: dirty worktree remains."
}

Write-Host "[$WorkerName] finished. HEAD $(((& git rev-parse HEAD) -join '').Trim())"
& git status --short
