# Secret and credential scan

The V2 review snapshot was scanned before publication. The scan scope was the
review tree only; authentication files, GitHub hosts/configuration, SSH keys,
runtime logs, and conversation/session files were not copied into it.

Checks performed:

- searched tracked snapshot text for `GH_TOKEN`, `GITHUB_TOKEN`, `OPENAI_API_KEY`,
  `CODEX_ACCESS_TOKEN`, bearer-token forms, GitHub personal-token prefixes,
  private-key headers, and credential-bearing filenames;
- verified worker environment code removes GitHub token and SSH-agent variables;
- verified worker `GH_CONFIG_DIR` is the empty
  `/var/lib/skyrim-dev/worker-gh-config`, not `/home/skyrimdev/.config/gh`;
- verified the redacted state projection contains no event history, worker log
  content, or authentication/configuration material.

Result: no credential value or private-key material was found in the review
snapshot. The same-account filesystem residual risk is documented in
[residual-risks.md](residual-risks.md).

The broad marker scan reported `supervisor.py` only because the source contains
the literal environment-variable names that it deliberately removes. A
follow-up value/assignment scan found no value after those names.
