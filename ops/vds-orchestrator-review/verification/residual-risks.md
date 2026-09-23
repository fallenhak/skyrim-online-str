# Residual risks for the V2.1 control-plane review

- Workers and the outer supervisor still use the same `skyrimdev` Unix account.
  Worker environment isolation hides GitHub CLI credentials and SSH-agent
  variables, but same-account readable files, including Codex authentication,
  are not a filesystem boundary.
- The configured focused-validation command map is empty for the current four
  phases. The supervisor therefore records focused tests as not independently
  verified and relies on worker evidence plus required GitHub CI.
- Recovery context is intentionally bounded and redacted. If a failure is
  emitted only outside the persisted last-error, CI excerpt, and bounded worker
  tail, the recovery worker will not receive that omitted detail and must stop
  safely rather than query GitHub.
- Textual untracked files are included only up to the bounded per-file review
  size. Binary, oversize, unreadable, and unsafe-path files are metadata-only
  and route to human review; this preserves safety at the cost of automatic
  throughput for legitimate binary assets.
- `gh run list` exposes the newest run records returned by the installed GitHub
  CLI. GitHub-side workflow naming/rerun behavior remains an external contract;
  the supervisor fails closed on missing, incomplete, or failed required runs.
- V2 does not enable or start 24/7 autonomous development. A later operator
  start still requires the explicit normal operational decision and review of
  this snapshot.
- Milestone 1 acceptance remains a real multi-client runtime/integration
  scenario; supervisor queue completion is not product acceptance.
- The control-plane worktree and GitHub fetch depend on the configured
  `skyrimdev` GitHub credential helper. A fetch or fast-forward failure fails
  closed and retains the last valid snapshot, but an operator must resolve the
  external Git condition before new roadmap state can apply.
- External gates are intentionally explicit and currently unresolved for
  M01-WORLD. No automatic branch creation or integration inference is
  attempted; the architect must publish the reviewed branch/gate state.
- The roadmap parser and scheduler are deterministic unit-tested, but actual
  W01-W10 runtime behavior is not proven by this installation and no Linux
  check is treated as Windows Skyrim acceptance evidence.
- Node 20/pnpm 9 tooling is installed from the repository's explicit workflow
  contract, but UI dependencies were not installed or built during this
  control-plane pass.
- The official scoped `bwrap-userns-restrict` AppArmor profile is now loaded and
  enforced, and the direct bwrap probes plus the disposable worker smoke test
  pass. The global AppArmor service and
  `kernel.apparmor_restrict_unprivileged_userns=1` remain security boundaries;
  future package/kernel/AppArmor changes require the same architect review.
- Worker and supervisor processes still share the `skyrimdev` Unix account, so
  the successful sandbox smoke test does not create a complete filesystem/user
  isolation boundary. Codex authentication remains available to the worker by
  design, while GitHub/SSH credentials remain filtered.

## V3 repair-specific residuals

- The durable request inbox is outside worker worktrees and the production
  `workspace-write` smoke probe is blocked, but a future change to the Codex
  sandbox or same-account permissions must preserve that negative regression.
- Processed request history and receipts are intentionally bounded. Requests
  older than the retained history are not an infinite audit ledger; durable
  archived evidence must be exported if longer retention is required.
- A crash before the daemon's atomic state commit can leave the request in the
  inbox for a safe retry. The exactly-once guarantee begins at the persisted
  processed-request record and prevents replay after that commit.
- `COMPLETE_WITH_VALIDATION_GAP` remains a claim by the worker until local
  structural checks and required CI pass; any actual failure invalidates the
  gap and follows recovery/review.


## Autonomous Sol reviewer — 2026-09-23

The Sol reviewer deliberately receives only the bounded inline evidence bundle
and has no file, shell, MCP, browser, plugin, or network tools. That limits its
ability to discover context omitted from the bundle; the bundle builder includes
the exact diff, bounded changed-file contents, validation, CI, roadmap and
product context, and cross-lane interfaces. If the bundle is oversized,
malformed, stale, or incomplete, the review fails closed for human or worker
recovery instead of granting access to additional files. A real gpt-6-sol/max
smoke run verified a strict `RETRY` response for a synthetic unsafe fixture and
no tool events. Human runtime acceptance remains required for M01.

- The resource guard paused global work twice during the validation window after classifying memory as unsafe. It stopped active Luna workers with SIGTERM while preserving their dirty diffs. After available memory recovered to about 3.4 GiB and healthchecks passed, the supervisor was resumed; no memory threshold was relaxed. Heavy validation alongside two active workers can trigger another safety pause.
