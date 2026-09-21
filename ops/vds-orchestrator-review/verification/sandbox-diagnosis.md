# Codex workspace-write sandbox diagnosis

The initial diagnosis was captured from the production VDS on 2026-09-22
before the architect-approved scoped remediation. The final remediation changed
only the required AppArmor package/profile state; kernel, systemd, capability,
and global namespace policy were not relaxed.

## Installed components

    OS: Ubuntu 24.04.1 LTS
    kernel: 6.8.0-41-generic x86_64
    virtualization: vmware
    Codex CLI: 0.155.1
    bubblewrap: 0.9.0
    unshare: util-linux 2.39.3

Final package versions:

    apparmor: 4.0.1really4.0.1-0ubuntu0.24.04.7
    apparmor-profiles: 4.0.1really4.0.1-0ubuntu0.24.04.7
    apparmor-utils: 4.0.1really4.0.1-0ubuntu0.24.04.7
    bubblewrap: 0.9.0-1ubuntu0.3

`apt update` and a package-scoped `apt install` were used. No distribution
upgrade, Docker installation, kernel command-line change, or global sysctl
change was performed.

The production worker settings were used: `gpt-5.6-luna`, reasoning `max`,
`approval_policy=never`, `--sandbox workspace-write`, `--ephemeral`, and a
service-user-owned scratch workspace.

## Reproduction before remediation

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

The pre-remediation smoke directory contained only the harmless input and
Codex output log; `output.txt` was not created. The directory was removed after
the test.

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

## Architect-approved remediation

The official Ubuntu/OpenAI-documented profile was present after installing
`apparmor-profiles`:

    source:    /usr/share/apparmor/extra-profiles/bwrap-userns-restrict
    installed: /etc/apparmor.d/bwrap-userns-restrict
    mode/owner: 0644 root:root
    SHA-256:   11d39094f044f0cda0febb3ad517b830301da6b2ce929664af09ee9e4dd264f9

It was copied exactly, loaded with `apparmor_parser -r` successfully, and
parse-only validation with `apparmor_parser -Q -T` returned `0`. Kernel profile
inspection shows both `bwrap (enforce)` and `unpriv_bwrap (enforce)`.

The following restrictions were intentionally preserved; the repair does not:

- disable AppArmor or the user-namespace restriction;
- grant `CAP_SYS_ADMIN`/`CAP_NET_ADMIN` or set capabilities on bubblewrap;
- run Codex as root or with `danger-full-access`;
- bypass the sandbox with `--dangerously-bypass-approvals-and-sandbox`; or
- expose the supervisor's GitHub/SSH credentials to a worker.

Global AppArmor remains enabled and
`kernel.apparmor_restrict_unprivileged_userns=1` remains unchanged. The global
`kernel.unprivileged_userns_clone=1` value also remains unchanged.

## Post-remediation probes

As `skyrimdev`, both disposable direct probes returned exit code `0` with no
error output:

    bwrap --unshare-user --ro-bind / / /bin/true
    rc=0

    bwrap --unshare-user --unshare-net --ro-bind / / /bin/true
    rc=0

The production `skyrim-dev worker-smoke-test` then passed with
`WORKER_SMOKE_OK`; see `worker-smoke-results.md`.
