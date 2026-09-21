# Residual risks for architect review

- Workers and the outer supervisor still use the same `skyrimdev` Unix account.
  Worker environment isolation hides GitHub CLI credentials and SSH-agent
  variables, but same-account readable files, including Codex authentication,
  are not a filesystem boundary.
- The configured focused-validation command map is empty for the current four
  phases. The supervisor therefore records focused tests as not independently
  verified and relies on worker evidence plus required GitHub CI.
- `gh run list` exposes the newest run records returned by the installed GitHub
  CLI. GitHub-side workflow naming/rerun behavior remains an external contract;
  the supervisor fails closed on missing, incomplete, or failed required runs.
- V2 does not enable or start 24/7 autonomous development. A later operator
  start still requires the explicit normal operational decision and review of
  this snapshot.
- Milestone 1 acceptance remains a real multi-client runtime/integration
  scenario; supervisor queue completion is not product acceptance.
