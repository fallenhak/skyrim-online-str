# VDS final-review-retry deployment - 2026-09-24

## Source and installation

- Source: codex/final-review-retry commit 17143e8b42b37630d7a1cbee39a1d1c18eb70d71, parent 79839da6b20949f0f927c5fca6d49e73e8526c70.
- VDS install backup: /var/backups/skyrim-dev/codex-final-retry-20260924T090409Z. It contains the five pre-install files, a verified SHA256SUMS, DEPLOYMENT.txt, and installed.sha256 for the deployed files.
- Installed mapped files: supervisor.py, architect_review.py, test_supervisor.py, test_architect_review.py, and /usr/local/bin/skyrim-dev. Their SHA-256 values exactly match the source checkout:

| File | SHA-256 |
| --- | --- |
| supervisor.py | 6f0990fca53467b51121e50f6ca5bba9e8295d5d50a0180adb0cc4f6274dfc08 |
| architect_review.py | 01eb02a156577a4d6963100427b0e2c3b104a8c0e8c7ef887f38a5cb047ef4ce |
| test_supervisor.py | 446ef47426f832198f0d1ae40866bd4c24c548ddfd18f6c224e0e3456affcfc1 |
| test_architect_review.py | 931448785bcbebb312c8d2bb95d4a5b67298dbcd41d3603b416abf61bd9b2f94 |
| skyrim-dev | f81bbc61f3657db04474882e9ec07e0660483281c305add58a30c46d97fb4c89 |

## Verification

- Separate VDS clone at the exact source commit: py_compile passed; full python3 -B -m unittest discover suite passed, 227 tests.
- Installed runtime: compile check passed; full python3 -B -m unittest discover suite passed, 227 tests.
- skyrim-dev self-test: SELF_TEST_OK.
- skyrim-dev healthcheck: all five checks passed.
- The refreshed verification/SHA256SUMS manifest passes sha256sum -c for every listed file.
- The orchestrator service is active. No lane worker or Sol reviewer was active at the final status capture.

## Final review recheck status

While paused, status and review-status reported the exact target commits below; each lane worktree HEAD matched its recorded SHA:

- Combat C17: fd1ebba8099a2d6ed2c054f6504b6fe551f09f22.
- Population L16: b7579be477375783b5e149bad6e1c096ec9965ac.

The combined skyrim-dev re-review-final request was rejected with "final review recheck refused while another lane awaits review". Authority A12 and UI U16 were still NEEDS_SOL_REVIEW; both had REQUIRES_INFRA_REVIEW evidence errors EXACT_SHA_CI_WORKFLOW_MISSING after three attempts. No target was queued and no review decision or approval was applied. Combat and Population therefore remain BLOCKED at the exact SHAs above, with FINAL_MILESTONE_OR_QUEUE_REVIEW.

The explicit resume completed. Final status was GLOBAL: RUNNING; there were zero active or queued Sol reviews. The scheduler reported that Authority and UI evidence failures require infrastructure review. No main or lane branch was changed.

## Follow-up deployment and review completion - 2026-09-24

- Exact-SHA CI existed on GitHub for both blocked current reviews. `gh run list --repo fallenhak/skyrim-online-str --commit 8460cb87cccefb9e17abbcc060a244ad3f2c8cce` showed successful `Build windows` run 35918523713 and `Build linux` run 35918523718. The corresponding query for `8b8d71a9b106dd6a3492b536e75ace889b9a3773` showed successful `Build linux` run 35936472692 and `Build windows` run 35936472597. Both lane refs contained the required workflow files. The original failure came from stale `lane.ci` state (`NOT_RUN`): CI polling only ran in `WAITING_FOR_CI`, while A12/U16 were already awaiting review, and review bundling did not refresh GitHub Actions evidence.
- After CI refresh was fixed, repeated bundle assembly exposed a second blocker: the review packet embedded a fresh `Generated` time and exact-SHA CI refresh rewrote `observed_at` even when all run evidence was unchanged. That changed the immutable review identity while a reviewer was working and produced stale items / `review_state_sha256 mismatch`. Commit `8d2c64d3` removes the packet timestamp and preserves the CI observation time when the exact workflow aggregate is unchanged. Regression coverage verifies both cases.
- Source was pushed only to `codex/final-review-retry` at `8d2c64d3`; no main or lane branch was changed by this repair. The pre-install VDS backup is `/var/backups/skyrim-dev/review-stable-bundle-20260924T102704Z`; its five-file `SHA256SUMS` passed verification.
- Staging and installed VDS suites each passed all 232 tests. `py_compile`, `re-review-current --help`, `sh -n /usr/local/bin/skyrim-dev`, `skyrim-dev healthcheck`, and `skyrim-dev self-test` passed. The service is active.
- While globally PAUSED, A12 exact SHA received a normal-tier `RETRY`, applied as bounded retry cycle 1/3. U16 exact SHA escalated from Luna to architect and received `BLOCK`; the recorded reason is unresolved UI failure-path behavior, with no focused UI tests in the phase evidence. No review decision was manually applied. After the requested resume, Authority was `CODING` and UI remained `BLOCKED`.
- `re-review-final` queued Combat C17 `fd1ebba8099a2d6ed2c054f6504b6fe551f09f22` as review `4b7ad6ab05a8ac75002b1caf8281f2020818ef21872d9a1b9fa1188d84a91b61` and Population L16 `b7579be477375783b5e149bad6e1c096ec9965ac` as review `097319e4b281e02ffde937e786ed7129bf591c004b6945039470b400009f9b2f` while PAUSED. Then `skyrim-dev resume` was processed. Final captured state: GLOBAL RUNNING; Combat `BLOCKED` with `APPROVED_NO_AUTOMATIC_NEXT_WORK`; Authority `CODING` with `LUNA_RETRY_CURRENT_PHASE`; Population `CODING` with `SOL_RETRY_FINAL_PHASE`; UI `BLOCKED` with `SOL_BLOCKED`. The four recorded last-commit SHAs still matched their exact targets at capture; review queue was empty and `evidence_errors: 0`.

### Post-resume review drain - 2026-09-24 10:53:56 UTC

After the first resumed retry workers returned without reviewable changes, the supervisor returned Authority A12 and Population L16 to normal `CURRENT_PHASE_REVIEW` on the same exact commits. The exact-SHA CI evidence remained PASS. Normal Luna reviews independently returned `RETRY` for both; at this capture both lanes were `CODING`, their last commits still matched the reviewed SHAs, and their workers were active. Combat remained `BLOCKED` with `APPROVED_NO_AUTOMATIC_NEXT_WORK`; UI remained `BLOCKED` with `SOL_BLOCKED`. GLOBAL was RUNNING, service active, reviewer queue empty, no active reviewer, and `evidence_errors: 0`.
