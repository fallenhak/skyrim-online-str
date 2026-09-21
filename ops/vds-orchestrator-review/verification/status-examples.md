# Scheduler status examples

Captured with the global mode still PAUSED and no runtime worker:

    CONTROL PLANE
    branch: orchestration/control-plane
    applied SHA: 3e7e893b4018b488e158aa5cda977399c0e55a75
    observed SHA: 3e7e893b4018b488e158aa5cda977399c0e55a75
    valid: yes

    MILESTONE M01: ACTIVE executable=True title=Core World
      M01-WORLD: lane=world mode=ROADMAP_TASKS state=BLOCKED_EXTERNAL_GATE

    W01 BLOCKED_EXTERNAL_GATE: unresolved external gate(s): reviewed integration branch containing required combat lifecycle and population-classification foundations

The lane view retains the exact queue positions and first-run review gates:

    combat     C03  NEEDS_SOL_REVIEW  CURRENT_PHASE_REVIEW
    authority  A04  NEEDS_SOL_REVIEW  CURRENT_PHASE_REVIEW
    population L03  PAUSED
    ui         U02  PAUSED

milestone-status reports M01 acceptance criteria and says runtime evidence is
required; it does not synthesize Windows evidence from Linux.

Repeated status, roadmap-status, milestone-status, review-status, healthcheck,
and self-test calls use observer supervisors and do not clear worker PIDs,
change `CODING` ownership, increment recovery counters, clear a live rate-limit
probe, or rewrite the persisted runtime identity.
