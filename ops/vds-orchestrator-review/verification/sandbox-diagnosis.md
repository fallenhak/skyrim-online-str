# Codex workspace-write sandbox diagnosis

Captured from the production VDS on 2026-09-22 without changing kernel,
AppArmor, systemd, capability, or namespace policy.

## Installed components

    OS: Ubuntu 24.04.1 LTS
    kernel: 6.8.0-41-generic x86_64
    virtualization: vmware
    Codex CLI: 0.155.1
    bubblewrap: 0.9.0
    unshare: util-linux 2.39.3

The production worker settings were used: `gpt-5.6-luna`, reasoning `max`,
`approval_policy=never`, `--sandbox workspace-write`, `--ephemeral`, and a
service-user-owned scratch workspace.

## Reproduction

The disposable command started Codex and produced the warning that Linux
sandbox setup requires user namespaces. The model could read the prompt but
the workspace command runner failed before creating the allowed output file.
The direct probes as `skyrimdev` reproduce the lower-level failure:

    bwrap --unshare-user --ro-bind / / /bin/true
    bwrap: setting up uid map: Permission denied

    bwrap --unshare-user --unshare-net --ro-bind / / /bin/true
    bwrap: loopback: Failed RTM_NEWADDR: Operation not permitted

    unshare -Urn true
    unshare: write failed /proc/self/uid_map: Operation not permitted

The smoke directory contained only the harmless input and Codex output log;
`output.txt` was not created. The directory was removed after the test.

## Root cause

The kernel sysctls are not the limiting condition:

    kernel.unprivileged_userns_clone=1
    user.max_user_namespaces=47559
    user.max_net_namespaces=47559

Ubuntu AppArmor is enforcing the `unprivileged_userns` profile because
`kernel.apparmor_restrict_unprivileged_userns=1`. Kernel audit entries show the
transition from `unconfined` to `unprivileged_userns`, followed by denials for
`capability=21 (sys_admin)` and writes to `proc/<pid>/uid_map`. Bubblewrap's
user/network namespace and loopback setup consequently cannot complete for
the unprivileged `skyrimdev` process.

This is an environment compatibility/policy blocker, not a project source
failure. The VDS has no usable `CAP_SYS_ADMIN` for `skyrimdev`, and the service
already runs with `NoNewPrivileges=yes`.

## Remediation decision

No speculative or broad workaround was installed. In particular, the repair
does not:

- disable AppArmor or the user-namespace restriction;
- grant `CAP_SYS_ADMIN`/`CAP_NET_ADMIN` or set capabilities on bubblewrap;
- run Codex as root or with `danger-full-access`;
- bypass the sandbox with `--dangerously-bypass-approvals-and-sandbox`; or
- expose the supervisor's GitHub/SSH credentials to a worker.

A narrowly scoped official sandbox-compatible AppArmor policy may be evaluated
by the architect separately, but it was not safe to invent and deploy during
this pass. The resulting state is explicitly `SANDBOX_INFRA_BLOCKED`, and
autonomous development remains disabled.
