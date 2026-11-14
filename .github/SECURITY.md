# Security

## Report a vulnerability

Please report vulnerabilities privately through
[GitHub's security advisories](https://github.com/carroarmato0/vitals/security/advisories/new),
not in public issues, so they can be fixed before they're known.

Include the Vitals version (`vitals --version`), what an attacker can do, and
the steps to reproduce it.

## What counts

Vitals runs as your user. It reads `/proc` and `/sys`, and pauses or limits
apps through your own systemd user manager. A vulnerability is something that
lets another user or a less-trusted program gain access or control it didn't
already have through Vitals: for example, making it signal, pause, or limit a
process it shouldn't, or run a command it wasn't given.

Supported: the latest release.
