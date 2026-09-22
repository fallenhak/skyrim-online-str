# Daemon-owned operator requests

Implemented in `source/orchestrator/supervisor.py` and exposed through the
existing `source/management/skyrim-dev` wrapper.

## Contract

Mutating operator commands are `approve`, `retry`, `block`, `approve-task`,
`approve-control-plane`, `accept-milestone`, and `sync-control-plane`.
The CLI validates the command envelope, checks that
`skyrim-dev-orchestrator.service` is active, writes one atomically-created JSON
request, and waits for the matching receipt. If the daemon is inactive or the
receipt deadline expires, the CLI fails without editing `state.json`.

The request inbox is `/var/lib/skyrim-dev/operator-requests/inbox`, outside all
four worker worktrees. The inbox, receipt, and archive directories are owned by
`skyrimdev` and mode `0700`; request files and receipts are mode `0600`.
Requests contain a version, unique request ID, command, typed arguments,
creation timestamp, and submitting identity. Unknown commands, unsafe IDs,
wrong argument shapes, invalid timestamps, stale requests, and malformed JSON
fail closed.

## Ownership and exactly-once sequence

The daemon acquires the supervisor lock before constructing
`Supervisor(runtime_owner=True)`. On every loop it scans a bounded number of
inbox files and executes each valid request against its current in-memory
state. During dispatch, intermediate `event()` saves are deferred. The daemon
then records the request ID and bounded result in
`state.json`, atomically persists the resulting state, writes the durable
receipt, and archives the inbox file.

## Pre-commit durability failure

Before dispatching a valid request, the daemon snapshots the JSON state and
the active roadmap snapshot. Intermediate `event()` saves remain deferred.
If the final atomic state save returns false or raises, the daemon restores
both snapshots, clears the uncommitted save bookkeeping, leaves the request in
the inbox, emits no receipt, archives nothing, and stops processing further
requests in that tick. The next tick therefore cannot mistake an in-memory
processed-request record for a persisted one. A restart loads the last
persisted state and safely retries the retained request.

`sync-control-plane` is the one request with an external side effect. Its
pre-commit actions are safe to replay: fetch is repeatable, fast-forward-only
merge converges on the same remote SHA, and the control-plane cache writes are
content-addressed/idempotent. The daemon still rolls back its in-memory state
and roadmap snapshot when the state commit fails; the regression suite proves
the external merge is performed only once while a retry simply observes the
already-advanced external head.

On restart, a request left in the inbox whose ID is already in
`processed_operator_requests` causes the saved receipt to be re-emitted and is
never dispatched again. A crash before the state commit leaves the request in
the inbox for a safe retry; a crash after the state commit is replay-safe.
Processed records, receipts, and archived requests are bounded by the configured
history limit.

The observer path cannot enable writes. Read-only commands construct an
observer and retain `_state_write_enabled=False`; mutation commands no longer
construct an observer or call `save_state()` themselves.

The live deterministic fixture `infra-fixture-invalid-20260922` was rejected by
the daemon with return code 2 and archived without changing any lane. This
proves the installed inbox/receipt path is live while avoiding a real lane
approval, retry, or block.
