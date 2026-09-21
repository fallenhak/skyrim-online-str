param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$ExpectedBranch,
    [Parameter(Mandatory=$true)][ValidateSet("combat","authority","population","ui")][string]$WorkerName,
    [double]$Hours = 5.5,
    [string]$Model = "gpt-5.6-luna",
    [ValidateSet("low","medium","high","xhigh","max")][string]$Effort = "max"
)

$ErrorActionPreference = "Stop"

$Queues = @{
    combat = @(
        @{ Id = "C03"; Task = "Add server-owned actor lifecycle/incarnation generation immune to entt ID reuse; cleanup on removal." },
        @{ Id = "C04"; Task = "Define append-only validated hit-observation DTO with no CharacterId, XP, reward, authoritative damage, or client-selected kill attribution." },
        @{ Id = "C05"; Task = "Implement pure attacker authorization: InWorld, current owner/epoch, eligible persistent attacker identity resolved server-side." },
        @{ Id = "C06"; Task = "Implement pure target authorization: current lifecycle, trusted Creature, non-player/non-humanoid, range/cell plausibility." },
        @{ Id = "C07"; Task = "Add bounded replay/dedupe keyed by attacker authority incarnation plus target lifecycle." },
        @{ Id = "C08"; Task = "Revisit client HitEvent producer; implement only if current server IDs/epochs can be mapped safely." },
        @{ Id = "C09"; Task = "Add server observation handler; accepted observations become bounded pending observations only." },
        @{ Id = "C10"; Task = "Correlate pending observation with accepted canonical health decrease, never client damage magnitude." },
        @{ Id = "C11"; Task = "Correlate canonical alive-to-dead trusted Creature transition; duplicate death does nothing and death sender is not killer." },
        @{ Id = "C12"; Task = "Integrate existing CombatContributionLedger only for validated and correlated observations. NO XP." },
        @{ Id = "C13"; Task = "Add internal server-only creature-death contribution event/result containing server-resolved CharacterIds only." },
        @{ Id = "C14"; Task = "Stress ownership transfer and disconnect during combat; fix lifecycle/authority bugs found." },
        @{ Id = "C15"; Task = "Ensure removal, respawn and entity reuse clear lifecycle, replay, pending and contribution state." },
        @{ Id = "C16"; Task = "Malformed-input, bounded-memory and security regression pass for the touched combat path." },
        @{ Id = "C17"; Task = "Final independent combat-lane review and broad focused tests; fix concrete regressions only." }
    )
    authority = @(
        @{ Id = "A04"; Task = "Equipment and weapon-drawn stale-incarnation audit; add epoch only if same-client reacquisition creates a real stale-packet bug." },
        @{ Id = "A05"; Task = "Movement update payload authority: stale epoch/reacquisition, finite values, entity membership and payload bounds; fix clear issues." },
        @{ Id = "A06"; Task = "Faction mutation authority: owner/incarnation, malformed entries and persistent-player isolation." },
        @{ Id = "A07"; Task = "Research AddTarget semantics for caster-owned versus caster-less/environmental effects; build a pure authorization policy only if repository evidence supports it." },
        @{ Id = "A08"; Task = "RemoveSpell, interrupt and spell-target interaction follow-up without breaking environmental semantics." },
        @{ Id = "A09"; Task = "Object Activate, LockChange and AssignObjects trust and sender-range audit; harden obvious spoof paths." },
        @{ Id = "A10"; Task = "ScriptAnimation, dialogue and subtitle presentation spoofing audit; add range/source checks where semantics are clear." },
        @{ Id = "A11"; Task = "Teleport, cell and reference-movement malformed-input and authority review outside combat-owned code." },
        @{ Id = "A12"; Task = "Bound client-controlled collections/maps on touched interaction paths where practical and test rejection." },
        @{ Id = "A13"; Task = "Session/InWorld and stale-entity removal review for all touched handlers." },
        @{ Id = "A14"; Task = "Protocol roundtrip, epoch and malformed-enum regression tests for touched interaction paths." },
        @{ Id = "A15"; Task = "Final independent interaction-authority security review; fix concrete regressions only." }
    )
    population = @(
        @{ Id = "L03"; Task = "Research TES4 plugin flags. Correctly identify ESL/light namespace including ESL-flagged .esp using plugin-header authority, not filename/client claim." },
        @{ Id = "L04"; Task = "Harden standard/light ID assignment and overflow/bounds; detect too many standard/light plugins cleanly." },
        @{ Id = "L05"; Task = "Verify master mapping/reference prefix resolution for NPC, RACE and ACHR across multiple masters and overrides; fix proven issues." },
        @{ Id = "L06"; Task = "Verify override precedence when later plugins override master-defined ACHR/NPC/RACE records." },
        @{ Id = "L07"; Task = "Ensure missing master and unresolved form behavior remains explicit Unknown, never guessed." },
        @{ Id = "L08"; Task = "Harden chunk/record bounds for minimal NPC/RACE/ACHR parsing against truncated or malformed plugin data without crashing." },
        @{ Id = "L09"; Task = "Filename case/path normalization and mapping between network ModsComponent names and server load metadata; avoid CR/path/case mismatches cross-platform." },
        @{ Id = "L10"; Task = "Improve Data directory/loadorder absence and partial-modlist diagnostics while keeping record loading opt-in." },
        @{ Id = "L11"; Task = "Add synthetic tests for standard plugins, true light plugins, ESL-flagged ESP, overrides, malformed records, duplicate/missing masters, unknown network IDs and spoofed prefix bits." },
        @{ Id = "L12"; Task = "Assess performance/memory of opt-in full record parsing; implement only low-risk bounded/indexing improvements justified by code evidence." },
        @{ Id = "L13"; Task = "Expand conservative population policy configurability without speculating edge races; preserve Unknown for unsupported classes." },
        @{ Id = "L14"; Task = "Verify humanoid assignment gate against hardened loader metadata and no-record default." },
        @{ Id = "L15"; Task = "Cross-platform GCC/MSVC/path portability pass for population loader changes." },
        @{ Id = "L16"; Task = "Final independent population parser/security review and focused tests; fix concrete regressions only." }
    )
    ui = @(
        @{ Id = "U02"; Task = "Document the exact Character Select UI state machine and native-to-Angular bridge contract for CharacterSummary/list/selection status." },
        @{ Id = "U03"; Task = "Implement the minimal typed native/client-to-Angular bridge for character list notifications and selection result using the existing protocol." },
        @{ Id = "U04"; Task = "Add Character Select route/screen with loading, empty, error and list states." },
        @{ Id = "U05"; Task = "Render server-provided character summaries only; no local authority or fake records." },
        @{ Id = "U06"; Task = "Wire selection action to the existing SelectCharacterRequest and prevent duplicate pending clicks." },
        @{ Id = "U07"; Task = "Handle selected, load-snapshot, ready and InWorld transitions; UI closes only at the correct state." },
        @{ Id = "U08"; Task = "Reconnect/disconnect/error reset; stale lists and pending selection must not survive a different connection." },
        @{ Id = "U09"; Task = "Add keyboard/controller navigation and focus/back behavior without bypassing session state." },
        @{ Id = "U10"; Task = "Remove or hide legacy party/co-op menu entry points that conflict with the new flow, without deleting PartyService backend." },
        @{ Id = "U11"; Task = "Audit and fix old party/player-list auto-open assumptions after world entry." },
        @{ Id = "U12"; Task = "Add UI/state tests where supported; otherwise isolate and test pure state reducers/services." },
        @{ Id = "U13"; Task = "Build skyrim_ui/client integration and fix concrete type/bridge issues." },
        @{ Id = "U14"; Task = "Handle zero characters, long names, invalid/failed selection and disconnect-during-selection edge states." },
        @{ Id = "U15"; Task = "Update UX/session docs and explicitly record the create-character gap." },
        @{ Id = "U16"; Task = "Final independent UI/state-machine review and focused regression pass." }
    )
}

$HardBoundaries = @{
    combat = "No ProgressionService award call, XP, loot, server-side Skyrim AI, client-selected CharacterId/damage/kill, PartyService or UI work."
    authority = "No XP/rewards, combat hit protocol, contribution ledger, creature lifecycle generation, ESLoader changes or UI. Do not blanket-block legitimate non-owner gameplay without repository evidence."
    population = "Record loading stays opt-in by default. Never classify Unknown as Creature/Humanoid. No combat/reward/inventory/magic/object/UI work."
    ui = "No fake local character creation, PartyService backend deletion, combat, authority, ESLoader or progression changes. Server character/session protocol remains authoritative."
}

if (-not (Test-Path $RepoRoot)) { throw "Missing worker repository: $RepoRoot" }
Set-Location $RepoRoot

$actualBranch = ((git branch --show-current) -join "").Trim()
if ($actualBranch -ne $ExpectedBranch) { throw "Wrong branch '$actualBranch'; expected '$ExpectedBranch'." }
if ((git status --porcelain)) { throw "Worker '$WorkerName' must start clean: $RepoRoot" }

git fetch origin
if ($LASTEXITCODE -ne 0) { throw "git fetch failed" }
git pull --ff-only origin $ExpectedBranch
if ($LASTEXITCODE -ne 0) { throw "git pull --ff-only failed" }

$CodexCommand = Get-Command codex -ErrorAction SilentlyContinue
if (-not $CodexCommand) { throw "codex CLI not found in PATH." }
$CodexExe = if ($CodexCommand.Source) { $CodexCommand.Source } else { $CodexCommand.Path }

$deadline = (Get-Date).AddHours($Hours)
$logRoot = Join-Path $env:TEMP ("skyrim-online-str-v2-" + $WorkerName)
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null

$queue = $Queues[$WorkerName]
$round = 0

foreach ($phase in $queue) {
    if ((Get-Date) -ge $deadline) { break }
    $round++
    $phaseId = $phase.Id
    $phaseTask = $phase.Task

    $prompt = @"
You are one autonomous engineering worker in the Skyrim Online STR project.

Worker lane: $WorkerName
Branch: $ExpectedBranch
Current phase: $phaseId
Exact phase task: $phaseTask

Read .codex/lane/PLAN.md first for lane ownership and context. Inspect current source, recent branch commits, tests and docs before editing.

IMPORTANT SANDBOX/GIT RULES:
- The Windows workspace-write sandbox intentionally protects .git and .codex. Do NOT attempt git add, commit, push, reset, clean, restore, checkout, branch switching, merge or rebase.
- Do NOT edit .codex files. The outer PowerShell supervisor will commit and push after you exit.
- Work only in ordinary source/test/docs files inside this repository.
- Complete ONLY phase $phaseId. Do not start the next phase.
- Leave a coherent, reviewable working-tree diff when finished.
- Run the narrow relevant tests/builds plus git diff --check where possible. Do not alter unrelated code just to make an aggregate build pass.
- The known unrelated immersive_launcher/loader/MemoryLayout.cpp MSVC C2127 failure must not be modified.
- If repository evidence shows the requested phase should be a no-op or documentation-only result, document the evidence in the relevant lane docs rather than inventing behavior.
- Preserve protocol compatibility unless this phase explicitly and safely requires an append-only protocol change.
- Treat all client input as untrusted across persistent/authority boundaries.

Hard lane boundary: $($HardBoundaries[$WorkerName])

Before exiting, re-review your own diff for security, lifecycle cleanup, bounded memory/input handling, cross-platform portability and tests. Do not claim tests passed unless you actually ran them.
"@

    $promptFile = Join-Path $logRoot ("round-{0:D2}-{1}-prompt.txt" -f $round, $phaseId)
    $stdoutFile = Join-Path $logRoot ("round-{0:D2}-{1}-stdout.log" -f $round, $phaseId)
    $stderrFile = Join-Path $logRoot ("round-{0:D2}-{1}-stderr.log" -f $round, $phaseId)
    Set-Content -LiteralPath $promptFile -Value $prompt -Encoding UTF8

    $reasoningConfig = 'model_reasoning_effort="' + $Effort + '"'
    $args = @("exec", "--sandbox", "workspace-write", "--model", $Model, "-c", 'approval_policy="never"', "-c", $reasoningConfig, "-")

    Write-Host "[$WorkerName] $phaseId starting..."
    $proc = Start-Process -FilePath $CodexExe -WorkingDirectory $RepoRoot -ArgumentList $args -RedirectStandardInput $promptFile -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile -NoNewWindow -PassThru -Wait
    Write-Host "[$WorkerName] $phaseId Codex exit code: $($proc.ExitCode)"

    if ($proc.ExitCode -ne 0) {
        Write-Warning "[$WorkerName] $phaseId failed. Dirty work is preserved; stopping this lane."
        git status --short
        break
    }

    git diff --check
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "[$WorkerName] $phaseId failed git diff --check. Dirty work is preserved; stopping this lane."
        break
    }

    $dirty = ((git status --porcelain) -join [Environment]::NewLine)
    if (-not $dirty) {
        Write-Host "[$WorkerName] $phaseId produced no diff; recording phase as evidence-only/no-op and continuing."
        continue
    }

    Write-Host "[$WorkerName] $phaseId diff:"
    git diff --stat
    git status --short

    git add -A
    if ($LASTEXITCODE -ne 0) { throw "git add failed for $WorkerName $phaseId" }

    git diff --cached --quiet
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[$WorkerName] $phaseId has no staged changes after add; continuing."
        continue
    }

    $commitMessage = "$WorkerName: complete $phaseId"
    git commit -m $commitMessage
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "[$WorkerName] $phaseId commit failed. Staged work is preserved; stopping this lane."
        break
    }

    git push origin "HEAD:$ExpectedBranch"
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "[$WorkerName] $phaseId push failed. Local commit is preserved; stopping this lane."
        break
    }

    Write-Host "[$WorkerName] $phaseId committed and pushed."
}

Write-Host "[$WorkerName] worker finished."
git log -5 --oneline
git status --short
