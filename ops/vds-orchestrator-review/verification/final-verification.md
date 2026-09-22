# Final V3 verification

Captured on 2026-09-22 after installation.

| Check | Result |
| --- | --- |
| `python3 -B -m unittest discover` on VDS | 92 passed |
| `skyrim-dev worker-smoke-test` | `WORKER_SMOKE_OK`; inbox probe `BLOCKED` |
| `skyrim-dev self-test` | `SELF_TEST_OK` |
| `skyrim-dev healthcheck` | all five checks passed |
| `skyrim-dev status` | `PAUSED`; all four lanes still pending review |
| `skyrim-dev roadmap-status` | valid; W01-W10 external-gated |
| `skyrim-dev milestone-status` | M01 `ACTIVE`; M02-M05 planned |
| `skyrim-dev review-status` | C04/L03/U02 current review; A04 checkpoint pending |
| V3.1 durability regression | rollback on failed save; no receipt/archive; same-daemon and restart replay safe |

The service is active in paused mode. The healthcheck timer is active and
enabled. No development worker PID or `codex exec` lane process exists.

The only live mutation fixture was an invalid request with a known safe ID and
an unknown lane. Its receipt was `FAILED`, return code `2`, with reason
`lane command requires one known lane`; it did not mutate lane state.

The V3.1 deterministic save-failure fixtures mutate only isolated test state.
They prove that a failed durable save restores the daemon state and roadmap
snapshot, leaves the request in the inbox without a receipt, and prevents the
next tick from treating the in-memory request marker as durable. The
`sync-control-plane` fixture models its real fetch/fast-forward/cache behavior
as an idempotent external action and proves safe replay after a pre-commit
failure.
