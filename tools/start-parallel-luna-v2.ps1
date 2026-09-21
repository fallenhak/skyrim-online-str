param(
    [double]$Hours = 5.5,
    [string]$Model = "gpt-5.6-luna",
    [ValidateSet("low","medium","high","xhigh","max")][string]$Effort = "max"
)

$ErrorActionPreference = "Stop"
$MainRepo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$WorkersRoot = Join-Path (Split-Path $MainRepo -Parent) "skyrim-online-str-workers"
$Runner = Join-Path $PSScriptRoot "run-codex-lane-v2.ps1"

$lanes = @(
    @{ Name = "combat"; Path = Join-Path $WorkersRoot "combat"; Branch = "parallel/combat-foundations" },
    @{ Name = "authority"; Path = Join-Path $WorkersRoot "authority"; Branch = "parallel/interaction-authority" },
    @{ Name = "population"; Path = Join-Path $WorkersRoot "population"; Branch = "parallel/population-loader" },
    @{ Name = "ui"; Path = Join-Path $WorkersRoot "ui"; Branch = "parallel/character-ui" }
)

foreach ($lane in $lanes) {
    if (-not (Test-Path $lane.Path)) { throw "Missing worktree: $($lane.Path)" }
    $dirty = ((git -C $lane.Path status --porcelain) -join [Environment]::NewLine)
    if ($dirty) {
        Write-Host ""
        Write-Warning "$($lane.Name) worktree is dirty; refusing to launch."
        git -C $lane.Path status --short
        throw "Clean all worker worktrees before starting v2."
    }
}

$children = @()
foreach ($lane in $lanes) {
    git -C $lane.Path fetch origin
    if ($LASTEXITCODE -ne 0) { throw "fetch failed for $($lane.Name)" }
    git -C $lane.Path pull --ff-only origin $lane.Branch
    if ($LASTEXITCODE -ne 0) { throw "pull failed for $($lane.Name)" }

    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", ('"' + $Runner + '"'),
        "-RepoRoot", ('"' + $lane.Path + '"'),
        "-ExpectedBranch", $lane.Branch,
        "-WorkerName", $lane.Name,
        "-Hours", $Hours,
        "-Model", $Model,
        "-Effort", $Effort
    )

    $p = Start-Process powershell.exe -ArgumentList $args -PassThru
    $children += [PSCustomObject]@{ Name = $lane.Name; Process = $p; Path = $lane.Path; Branch = $lane.Branch }
    Write-Host "Started $($lane.Name) supervisor PID $($p.Id)"
}

while ($true) {
    $alive = @($children | Where-Object { -not $_.Process.HasExited })
    if ($alive.Count -eq 0) { break }
    Start-Sleep -Seconds 15
}

Write-Host ""
Write-Host "All v2 lane supervisors exited."
foreach ($child in $children) {
    $head = ((git -C $child.Path rev-parse HEAD) -join "").Trim()
    Write-Host "$($child.Name): $head"
    $dirty = ((git -C $child.Path status --porcelain) -join [Environment]::NewLine)
    if ($dirty) {
        Write-Warning "$($child.Name) worktree is dirty: $($child.Path)"
        git -C $child.Path status --short
    }
}
