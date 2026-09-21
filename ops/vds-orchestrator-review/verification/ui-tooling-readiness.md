# UI tooling readiness

The current UI repository provides direct, compatible setup evidence:

- Code/skyrim_ui/package.json contains the Angular UI scripts and dependencies.
- Code/skyrim_ui/pnpm-lock.yaml is the package-manager lockfile.
- .github/workflows/windows.yml installs pnpm version 9.
- The same workflow selects Node node-version: lts/iron, i.e. the Node 20
  LTS line used by the repository's Windows build.

The VDS initially had no node, pnpm, or corepack. The minimal supported
tooling was installed:

    node v20.20.2
    npm 10.8.2
    pnpm 9.15.9

No UI dependencies were installed during this control-plane task. npm and pnpm
caches are directed to /var/cache/skyrim-dev/npm and
/var/cache/skyrim-dev/pnpm, owned by skyrimdev; the cache sizes were 9.6 MiB
and 4 KiB immediately after installation.
