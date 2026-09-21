param(
    [double]$Hours = 5.5,
    [string]$Model = "gpt-5.6-luna",
    [ValidateSet("low","medium","high","xhigh","max")][string]$Effort = "max",
    [string]$Lanes = "combat,authority,population,ui",
    [int]$StartDelaySeconds = 20
)

$ErrorActionPreference = "Stop"
$OrchestratorRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $OrchestratorRoot

$LaneMap = @{
    "combat" = @{
        Branch = "parallel/combat-foundations"
        Folder = "combat"
    }
    "authority" = @{
        Branch = "parallel/interaction-authority"
        Folder = "authority"
    }
    "population" = @{
        Branch = "parallel/population-loader"
        Folder = "population"
    }
    "ui" = @{
        Branch = "parallel/character-ui"
        Folder = "ui"
    }
}

$Selected = @()
foreach ($name in ($Lanes -split ",")) {
    $trimmed = $name.Trim().ToLowerInvariant()
    if (-not $trimmed) {
        continue
    }
    if (-not $LaneMap.ContainsKey($trimmed)) {
        throw "Unknown lane '$trimmed'. Valid: combat,authority,population,ui"
    }
    $Selected += $trimmed
}

if ($Selected.Count -eq 0) {
    throw "No lanes selected."
}

if (-not (Get-Command codex -ErrorAction SilentlyContinue)) {
    throw "codex CLI was not found in PATH."
}

& git fetch origin
if ($LASTEXITCODE -ne 0) {
    throw "git fetch origin failed"
}

$ParentFolder = Split-Path $OrchestratorRoot -Parent
$WorkersRoot = Join-Path $ParentFolder "skyrim-online-str-workers"
New-Item -ItemType Directory -Force -Path $WorkersRoot | Out-Null
$Runner = Join-Path $OrchestratorRoot "tools\run-codex-lane.ps1"

$Processes = @()
$WorkerInfo = @()

foreach ($name in $Selected) {
    $cfg = $LaneMap[$name]
    $branch = $cfg.Branch
    $path = Join-Path $WorkersRoot $cfg.Folder

    Write-Host ""
    Write-Host "Preparing lane '$name' -> $branch"
    Write-Host "worktree: $path"

    if (Test-Path $path) {
        & git -C $path rev-parse --is-inside-work-tree *> $null
        if ($LASTEXITCODE -ne 0) {
            throw "Existing path '$path' is not a git worktree. Move/remove it manually."
        }

        $existingBranch = ((& git -C $path branch --show-current) -join "").Trim()
        if ($existingBranch -ne $branch) {
            throw "Worktree '$path' is on '$existingBranch', expected '$branch'."
        }

        if (((& git -C $path status --porcelain) -join [Environment]::NewLine)) {
            throw "Worktree '$path' is dirty. Inspect it before restarting parallel workers."
        }

        & git -C $path fetch origin
        if ($LASTEXITCODE -ne 0) { throw "fetch failed in $name" }
        & git -C $path pull --ff-only origin $branch
        if ($LASTEXITCODE -ne 0) { throw "pull failed in $name" }
    }
    else {
        & git show-ref --verify --quiet "refs/heads/$branch"
        if ($LASTEXITCODE -eq 0) {
            & git worktree add $path $branch
        }
        else {
            & git worktree add -b $branch $path "origin/$branch"
        }
        if ($LASTEXITCODE -ne 0) {
            throw "git worktree add failed for $name"
        }
    }

    $WorkerCommand = '$host.UI.RawUI.WindowTitle = "Skyrim Online STR - ' + $name + '"; & "' + $Runner + '" -RepoRoot "' + $path + '" -ExpectedBranch "' + $branch + '" -WorkerName "' + $name + '" -Hours ' + ([string]$Hours) + ' -Model "' + $Model + '" -Effort "' + $Effort + '"'
    $ProcessArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-Command", $WorkerCommand
    )

    $proc = Start-Process -FilePath "powershell.exe" -ArgumentList $ProcessArgs -PassThru
    $Processes += $proc
    $WorkerInfo += @{
        Name = $name
        Branch = $branch
        Path = $path
        Pid = $proc.Id
    }

    Write-Host "Started '$name' worker PID $($proc.Id)"
    if ($StartDelaySeconds -gt 0) {
        Start-Sleep -Seconds $StartDelaySeconds
    }
}

Write-Host ""
Write-Host "============================================================"
Write-Host "PARALLEL LUNA/MAX WORKERS RUNNING"
Write-Host "hours: $Hours"
Write-Host "model: $Model"
Write-Host "effort: $Effort"
Write-Host "workers root: $WorkersRoot"
Write-Host "============================================================"

foreach ($info in $WorkerInfo) {
    Write-Host "$($info.Name): PID $($info.Pid) | $($info.Branch) | $($info.Path)"
}

Write-Host ""
Write-Host "This launcher will monitor all worker processes."
Write-Host "If one worker exits early, the other workers will keep running."
Write-Host "Do not let Windows sleep."

$ReportedExit = @{}
while ($true) {
    $runningCount = 0

    for ($i = 0; $i -lt $Processes.Count; $i++) {
        $proc = $Processes[$i]
        $info = $WorkerInfo[$i]
        $proc.Refresh()

        if ($proc.HasExited) {
            if (-not $ReportedExit.ContainsKey($proc.Id)) {
                $ReportedExit[$proc.Id] = $true
                Write-Warning "$($info.Name) worker exited with code $($proc.ExitCode) (PID $($proc.Id)). Other workers continue."
            }
        }
        else {
            $runningCount++
        }
    }

    if ($runningCount -eq 0) {
        break
    }

    Start-Sleep -Seconds 15
}

Write-Host ""
Write-Host "All selected workers exited."

foreach ($info in $WorkerInfo) {
    $head = ((& git -C $info.Path rev-parse HEAD) -join "").Trim()
    $dirty = ((& git -C $info.Path status --porcelain) -join [Environment]::NewLine)
    Write-Host "$($info.Name): $head"
    if ($dirty) {
        Write-Warning "$($info.Name) worktree is dirty: $($info.Path)"
    }
}
