# Secret and credential scan — runtime-owner/sandbox repair

The review snapshot was scanned after the supervisor/test/doc updates and
before publication. The scan scope was the review tree only; authentication
files, GitHub hosts/configuration, SSH keys, runtime logs, and
conversation/session files were not copied into it.

Checks performed:

- searched tracked snapshot text for `GH_TOKEN`, `GITHUB_TOKEN`, `OPENAI_API_KEY`,
  `CODEX_ACCESS_TOKEN`, bearer-token forms, GitHub personal-token prefixes,
  private-key headers, and credential-bearing filenames;
- verified worker environment code removes GitHub token and SSH-agent variables;
- verified worker `GH_CONFIG_DIR` is the empty
  `/var/lib/skyrim-dev/worker-gh-config`, not `/home/skyrimdev/.config/gh`;
- verified the redacted state projection contains no event history, worker log
  content, or authentication/configuration material.
- verified recovery prompt evidence is bounded and passed through the existing
  redaction helper, and that binary/oversize untracked content is represented
  only by metadata.
- verified the worker smoke command is scratch-only, uses no Git operation, and
  does not expose GitHub or SSH credential variables.

Result: no credential value or private-key material was found in the
review snapshot. The same-account filesystem residual risk and the scoped
AppArmor/bubblewrap boundary are documented in
[residual-risks.md](residual-risks.md). The post-remediation smoke result was
`WORKER_SMOKE_OK`.

The broad marker scan reported `supervisor.py` only because the source contains
the literal environment-variable names that it deliberately removes. A
follow-up value/assignment scan found no value after those names.

## V3 request-boundary additions

- The review tree contains no request receipts, runtime logs, SSH material,
  GitHub configuration, or raw state; only the redacted fixture result is
  documented.
- Request IDs are filename-safe and bounded; malformed, stale, unknown, and
  unsupported envelopes fail closed before dispatch.
- The inbox/receipts/archive directories are
  `/var/lib/skyrim-dev/operator-requests` with owner `skyrimdev` and mode
  `0700`; the inbox is not below any worker worktree.
- The production Luna `workspace-write` smoke test returned
  `WORKER_SMOKE_OK` and `operator request inbox from worker: BLOCKED`.
- The source-level CLI route contains no independent observer `save_state()`
  mutation path; only the lock-owning daemon records operator results.
