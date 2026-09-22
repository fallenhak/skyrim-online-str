# Phases 5–10 maintenance and recovery record

Captured 2026-09-23 01:17 +03:00.

## Phase 1 baseline (before the Codex update)

- `skyrim-dev status`: GLOBAL PAUSED (operator requested pause); service active; control plane VALID at 3e7e893b4018b488e158aa5cda977399c0e55a75. Combat C04/e54145143df19f55b7e9556120e3c01fab5d5ccd, Authority A05/d44b54cafcca9911a0e77268125b41ee4c078b97, Population L03/52c97ba4d5da993e6ef2fa4bdf398f13c22b456a, and UI U02/74184a23ac7d3e6d7f917b69a536bfd4603ea5a2 were all awaiting their expected reviews; no worker PID was running.
- `systemctl status skyrim-dev-orchestrator.service --no-pager --full`: active/running, MainPID 184985, ExecStart /usr/bin/python3 /srv/services/skyrim-dev/orchestrator/supervisor.py run.
- `codex --version`: codex-cli 0.155.1. `readlink -f /usr/local/bin/codex`: /opt/codex/packages/standalone/releases/0.155.1-x86_64-unknown-linux-musl/bin/codex.
- Baseline modes/owners: /opt/codex root:root 755; /usr/local/bin/codex symlink root:root; repository .git and .git/objects skyrimdev:skyrimdev 755.
- Baseline `git status --short`: Combat clean; Authority clean; Population had exactly six unstaged modifications (listed in the object-store section); UI clean. Baseline worker concurrency configuration was 2.

## Runtime and source

- systemd unit: /etc/systemd/system/skyrim-dev-orchestrator.service; User=skyrimdev, Group=skyrimdev; ExecStart=/usr/bin/python3 /srv/services/skyrim-dev/orchestrator/supervisor.py run.
- Installed config: /srv/services/skyrim-dev/config/supervisor.json. max_concurrent_workers remains 2.
- Canonical tracked source: ops/vds-orchestrator-review/source/orchestrator/supervisor.py on infra/vds-orchestrator-review. The branch was at a6072335a8295a259b29bef9c8818579ce8bfe4d before this work, with a clean worktree and two local commits ahead of origin.
- The source map designates this snapshot as the source for the installed service files. No deployment script was present in the tracked infra snapshot or installed service tree. The daemon loads the installed path above.
- The installed supervisor already had a hard worker-admission cap and routed recovery admission through schedule(); those safeguards were absent from the tracked snapshot. The tracked source and regression tests now include the running implementation so a source-based redeploy retains the recovery cap.
- Only the two development worker model arguments changed from gpt-5.6-luna to gpt-6-luna: normal worker start and disposable worker smoke. Both still pass model_reasoning_effort="max". The max worker setting is still 2. No Codex config, Sol/review model setting, approval policy, sandbox policy, or review gate was edited. Existing worker approval_policy="never" and workspace-write sandbox arguments remain in place.
- Canonical source SHA-256: 0d630be2ada352c16e324faad7e5a498d9df5f073ef0817b0b02156d942894e1. Installed source SHA-256: 8e95d50e9f1e4a9f1f4543f80e73ee0a7c01cccae5b2dab0a02dd0e31405b6bb; content matches canonical source after normalizing the installed CRLF line endings.
- Exact pre-edit copies are retained under /var/backups/skyrim-dev/phase5-20260923. The backup hashes were recorded before editing.

## Codex and validation

- The official managed standalone installer updated Codex CLI from 0.155.1 to codex-cli 0.156.0 at /opt/codex/packages/standalone/releases/0.156.0-x86_64-unknown-linux-musl/bin/codex. /usr/local/bin/codex resolves through /opt/codex/packages/standalone/current to that release; root and skyrimdev both reported 0.156.0. No global npm Codex package was present.
- A real non-interactive probe used model gpt-6-luna, reasoning max, read-only sandbox, and the existing skyrimdev CODEX_HOME; it exited 0 and returned MODEL_OK.
- Python compile check passed for supervisor.py, test_supervisor.py, roadmap.py, and test_roadmap.py.
- The deterministic supervisor and roadmap suite passed 101 tests after the permission correction.
- skyrim-dev self-test returned SELF_TEST_OK after the model update and again after the permission correction.
- skyrim-dev worker-smoke-test returned WORKER_SMOKE_OK. Local scratch read/write passed; the operator inbox write was blocked; no development worktree or branch changed.
- The model regression tests assert that both spawned workers use gpt-6-luna with model_reasoning_effort="max", and that the smoke command uses the same model and reasoning setting.

## Population Git object store

- The supervisor and Population worktree both run as skyrimdev. Population's .git file points to /srv/projects/skyrim-online-str/repo/.git/worktrees/population; commondir points to the shared /srv/projects/skyrim-online-str/repo/.git. The worktree index is separate from the common object database. core.sharedRepository is 0.
- The repository, .git directory, objects parent, Population worktree, and Population worktree admin directory are owned by skyrimdev:skyrimdev. The failed ESLoader.cpp blob hashes to prefix 15. That existing .git/objects/15 directory was root:root mode 755, so the service user could read but could not create the loose object there. The same prospective file set also hashes to existing root-owned prefixes 21 and 1b.
- Initial reproduction implicated .git/objects/15, .git/objects/21, and .git/objects/1b, all root:root mode 755. After the same failure appeared in the resumed Authority A06 worker on CharacterService.cpp, the remaining root-owned prefixes were also corrected. In total, ownership changed on all 20 previously root-owned two-character loose-object fan-out directories to skyrimdev:skyrimdev; all modes remained 755. No loose-object file, project file, worktree index, or other project permission was changed by this repair. A fresh scan found 201 fan-out directories, zero root-owned directories, and no nonempty xattrs. getfacl is not installed.
- Before and after the repair, Population status was exactly six unstaged modifications and HEAD remained 52c97ba4d5da993e6ef2fa4bdf398f13c22b456a. SHA-256 values remained:
  - Code/components/es_loader/ESLoader.cpp: 402d0f1201593152a56e6940ff09e070443612fd821d2161d4515b9e6e908c5a
  - Code/components/es_loader/Records/Record.h: 057073db535512aa4e5a69b4176908ca970b1de84349b2f55c1385d791a48bdd
  - Code/components/es_loader/TESFile.cpp: af0a94e44c5958457d324c8eab4c24fd9c4568940be5848a75e73f1e99e41024
  - Code/components/es_loader/TESFile.h: f2b70f1e19c096c994d40ae97323b3857cfef2cd4450dad65b8e8c8f1473f66a
  - Code/tests/ActorPopulationTests.cpp: 3797879b4df3931764e6fc0eb5b1be78c4dc638974bff15b90eac17795211ba5
  - docs/ACTOR_POPULATION.md: 42699d44f929b182042b89dff7a9ffebd6d8bdf882090b9a140006934fcaadae
- The real Population index SHA-256 stayed 8005865fcb57041dbef30ce4dfca87fdcef96d16fee2ad1a740edd9d2059546f; its logical ls-files snapshot stayed 2332b1faa0dd6698bcd4377ee9d22cd44c9533634d7e5c28056b531e91c03ea8.
- The installed prospective_commit_diff_check was rerun as skyrimdev with an isolated temporary index and the six explicit paths. Result: PASS, diff_check=true, real_index_unchanged=true. All six prospective blob objects are readable from the common object store afterward.

## Review gates and resume observation

- Combat C04/e54145143df19f55b7e9556120e3c01fab5d5ccd, both required CI workflows PASS, POST_PHASE_CHECKPOINT, next C05: approve succeeded (request ce07128754f04b88adac77566686451f).
- Authority A05/d44b54cafcca9911a0e77268125b41ee4c078b97, both required CI workflows PASS, POST_PHASE_CHECKPOINT, next A06: approve succeeded (request 94f1daf7d1754df79757f57520436553).
- UI U02/74184a23ac7d3e6d7f917b69a536bfd4603ea5a2, both required CI workflows PASS, POST_PHASE_CHECKPOINT, next U03: approve succeeded (request 0de4ec4372a24094ae2d286d9af35462).
- Population remained L03 at the expected HEAD with CURRENT_PHASE_REVIEW and all six modifications; after the object insertion check passed, retry succeeded (request d5030b17e6654553ad65895b36c7746). Population was never approved.
- Phase 8 status passed with GLOBAL PAUSED, service active, control plane VALID at 3e7e893b4018b488e158aa5cda977399c0e55a75, C05/A06/U03 paused, Population paused with paused_from_state=RECOVERING, and no development worker PID.
- After resume, 31 samples were collected at 10-second intervals over 300.3 seconds. GLOBAL remained RUNNING, service active, control plane VALID, and maximum observed development concurrency was 2. Three worker process command lines were observed: UI PID 212496, Combat PID 212532, and Authority PID 214361. Each used gpt-6-luna and model_reasoning_effort="max".
- With two slots occupied, Population remained RECOVERING/READY without a worker PID. Combat entered NEEDS_SOL_REVIEW and used no slot. No review packet was approved after resume.
- Full sample evidence is stored at /var/backups/skyrim-dev/phase5-20260923/phase9-observation.jsonl.

## State at the end of the five-minute sample

At approximately 01:15 +03:00, status was GLOBAL RUNNING, service active, and control plane VALID at 3e7e893b4018b488e158aa5cda977399c0e55a75. Combat C05 had entered NEEDS_SOL_REVIEW without a PID; Authority A06 and UI U03 were coding with PIDs 214361 and 212496; Population L03 remained RECOVERING without a PID. This was the end of the 31-sample, 300.3-second observation.

## Intermediate state captured at 01:24 +03:00

- GLOBAL RUNNING; service active; control plane VALID at 3e7e893b4018b488e158aa5cda977399c0e55a75.
- Combat C05 is NEEDS_SOL_REVIEW with no PID. Its new packet was not approved.
- Authority A06 is CODING, PID 214361.
- Population L03 is NEEDS_SOL_REVIEW with no PID and packet population-L03-1790115734.md. It was not approved.
- UI U03 is WAITING_FOR_CI with no PID; its current HEAD is 4093a55b435ab9f02d8ee6788861deaa7f8e768f, and both required workflows are in progress.
- One development worker was live in the latest process snapshot. Maximum concurrency during the bounded sample remained 2.
- At that intermediate capture, the infrastructure source, tests, changelog, README, checksum manifest, and this report were still uncommitted. The installed service source had been updated and restarted successfully.

Latest git status --short:

Combat:
A  Code/server/Services/CombatAttackerAuthorizationPolicy.h
A  Code/tests/CombatAttackerAuthorizationPolicyTests.cpp
M  docs/COMBAT_AUTHORITY.md

Authority:
 M Code/client/Services/Generic/CharacterService.cpp
 M Code/encoding/Messages/AssignCharacterRequest.cpp
 M Code/encoding/Messages/AssignCharacterRequest.h
 M Code/encoding/Messages/CharacterSpawnRequest.cpp
 M Code/encoding/Messages/CharacterSpawnRequest.h
 M Code/encoding/Messages/NotifyFactionsChanges.cpp
 M Code/encoding/Messages/NotifyFactionsChanges.h
 M Code/encoding/Messages/RequestFactionsChanges.cpp
 M Code/encoding/Messages/RequestFactionsChanges.h
 M Code/encoding/Structs/CharacterData.cpp
 M Code/encoding/Structs/Faction.cpp
 M Code/encoding/Structs/Faction.h
 M Code/encoding/Structs/Factions.cpp
 M Code/encoding/Structs/Factions.h
 M Code/server/Services/CharacterService.cpp
 M docs/ACTOR_AUTHORITY_AUDIT.md
?? Code/encoding/Structs/FactionAuthorityPolicy.h
?? Code/tests/FactionAuthorityPolicyTests.cpp

Population:
M  Code/components/es_loader/ESLoader.cpp
M  Code/components/es_loader/Records/Record.h
M  Code/components/es_loader/TESFile.cpp
M  Code/components/es_loader/TESFile.h
M  Code/tests/ActorPopulationTests.cpp
M  docs/ACTOR_POPULATION.md

UI:
(empty)

The branch HEADs at this capture were unchanged for Combat e54145143df19f55b7e9556120e3c01fab5d5ccd, Authority d44b54cafcca9911a0e77268125b41ee4c078b97, and Population 52c97ba4d5da993e6ef2fa4bdf398f13c22b456a. UI was at 4093a55b435ab9f02d8ee6788861deaa7f8e768f.

No development branch was merged or pushed. No reset, clean, checkout, restore, force-push, object deletion, repack, prune, or garbage collection was performed.

## Final follow-up captured at 2026-09-23 01:38 +03:00

- A fresh Population prospective_commit_diff_check after correcting all 20 directories returned PASS with diff_check=true and real_index_unchanged=true. The real Population index hash before and after this check was 4e3bdff95a560a9fbfe507bba59a9ea097f994d2e34fabe49f742e1598869163.
- The supervisor self-test was rerun after the full ownership correction and returned SELF_TEST_OK.
- The repository has 201 two-character loose-object fan-out directories. All are owned by skyrimdev:skyrimdev; their existing modes were retained. No nonempty xattrs were present. No root-owned fan-out directory remains.
- Latest status: GLOBAL RUNNING; service active; control plane VALID at 3e7e893b4018b488e158aa5cda977399c0e55a75. Combat C05 is NEEDS_SOL_REVIEW with no PID. Authority A06 is CODING with PID 219417 after its Linux CI workflow failed; Windows CI was still in progress at capture. Population L03 is NEEDS_SOL_REVIEW with no PID. UI U03 is NEEDS_SOL_REVIEW with no PID after both CI workflows passed. No new review packet was approved.
- One live development worker was present at capture: Authority PID 219417, model gpt-6-luna, model_reasoning_effort="max". Maximum concurrency in the five-minute sample remained 2; no review-gated lane used a slot.
- Latest development worktree HEADs: Combat e54145143df19f55b7e9556120e3c01fab5d5ccd; Authority 322562962508abae49d82839a745c5996de044e7; Population 52c97ba4d5da993e6ef2fa4bdf398f13c22b456a; UI 4093a55b435ab9f02d8ee6788861deaa7f8e768f.

Latest git status --short at this capture:

Combat:
A  Code/server/Services/CombatAttackerAuthorizationPolicy.h
A  Code/tests/CombatAttackerAuthorizationPolicyTests.cpp
M  docs/COMBAT_AUTHORITY.md

Authority:
(empty)

Population:
M  Code/components/es_loader/ESLoader.cpp
M  Code/components/es_loader/Records/Record.h
M  Code/components/es_loader/TESFile.cpp
M  Code/components/es_loader/TESFile.h
M  Code/tests/ActorPopulationTests.cpp
M  docs/ACTOR_POPULATION.md

UI:
(empty)

The bounded infrastructure source and verification record are committed locally to infra/vds-orchestrator-review. The commit is not pushed. No development branch was merged. No reset, clean, destructive checkout/restore, force-push, object deletion, repack, prune, or garbage collection occurred.

## Final live snapshot captured at 2026-09-23 01:48 +03:00

- GLOBAL RUNNING; supervisor service active; control plane VALID at 3e7e893b4018b488e158aa5cda977399c0e55a75.
- Combat C05, Authority A06, Population L03, and UI U03 are all NEEDS_SOL_REVIEW; each reports worker PID none. The current ps -C codex snapshot was empty, so there were zero live Codex development workers. No review packet created after resume was approved.
- Authority A06 Linux CI remains failed (run 35792965327); its Windows run 35792965343 is still in progress. Combat CI is PASS, Population CI is NOT_RUN with the recorded local tooling gap, and UI CI is PASS.
- Final git status --short:
  - Combat: staged additions Code/server/Services/CombatAttackerAuthorizationPolicy.h and Code/tests/CombatAttackerAuthorizationPolicyTests.cpp; staged modification docs/COMBAT_AUTHORITY.md.
  - Authority: clean.
  - Population: exactly six unstaged modifications: Code/components/es_loader/ESLoader.cpp, Code/components/es_loader/Records/Record.h, Code/components/es_loader/TESFile.cpp, Code/components/es_loader/TESFile.h, Code/tests/ActorPopulationTests.cpp, and docs/ACTOR_POPULATION.md.
  - UI: clean.
- Infrastructure review branch remains clean at local commit d1dcc1b93056719b931d940fb14a515da3f568e1, three commits ahead of origin. It has not been pushed.
- No development branch was merged. No reset, clean, destructive checkout/restore, force-push, object deletion, repack, prune, or garbage collection occurred.
