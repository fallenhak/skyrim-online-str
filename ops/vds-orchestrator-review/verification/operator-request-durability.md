# Operator-request durability regression

The V3.1 repair closes the interval between dispatch and the durable state
commit. A valid request now runs transactionally against a deep snapshot of
the daemon state and active roadmap snapshot. A failed `save_state()` rolls
back that memory, including `processed_operator_requests`, retains the inbox
file, emits no success receipt, and prevents another request from being
processed in the same tick.

The deterministic regression coverage proves:

- a mutation can occur in daemon memory before the simulated durable save
  fails;
- no receipt is emitted and the inbox file is not archived;
- a later tick in the same daemon dispatches again instead of trusting the
  rolled-back marker;
- a fresh daemon seeded from the last persisted state safely processes the
  retained request;
- after a successful save, duplicate and restart replay paths do not dispatch
  the logical command again;
- the existing stale-memory protection still persists the mutation and its
  processed-request record together; and
- `sync-control-plane` replay is safe because its fetch/fast-forward/cache
  effect is idempotent and convergent.

The V3.1 suite remains bounded to the review package and never exercises a
real development-lane approve, retry, block, advance, commit, push, or merge.
