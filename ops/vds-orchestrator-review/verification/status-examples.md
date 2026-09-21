# Scheduler status examples

Captured with the global mode still PAUSED:

    CONTROL PLANE
    branch: orchestration/control-plane
    applied SHA: 3e7e893b4018b488e158aa5cda977399c0e55a75
    observed SHA: 3e7e893b4018b488e158aa5cda977399c0e55a75
    valid: yes

    MILESTONE M01: ACTIVE executable=True title=Core World
      M01-WORLD: lane=world mode=ROADMAP_TASKS state=BLOCKED_EXTERNAL_GATE

    W01 BLOCKED_EXTERNAL_GATE: unresolved external gate(s): reviewed integration branch containing required combat lifecycle and population-classification foundations

The paused lane view retains the exact queue positions:

    combat     C03  PAUSED
    authority  A04  PAUSED
    population L03  PAUSED
    ui         U02  PAUSED

milestone-status reports M01 acceptance criteria and says runtime evidence is
required; it does not synthesize Windows evidence from Linux.
