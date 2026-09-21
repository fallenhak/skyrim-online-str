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
- The official Codex `workspace-write` sandbox is not usable on this VDS as
  currently configured. Ubuntu 24.04 AppArmor transitions unprivileged user
  namespaces into `unprivileged_userns`, whose policy denies the capability and
  `/proc/*/uid_map` operations bubblewrap needs; direct bwrap testing reports
  `Failed RTM_NEWADDR: Operation not permitted`. No broad AppArmor relaxation,
  setcap workaround, root worker, or unrestricted Codex mode was attempted.
  This is an explicit `SANDBOX_INFRA_BLOCKED` gate for autonomous development.
- The smoke command's local write proof therefore fails closed. The command is
  retained as a repeatable diagnostic and must pass before any worker phase is
  considered runnable again.
