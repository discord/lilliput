# Security Policy

## Supported versions

None. This repository is unmaintained as of October 2026 and no version
receives security fixes. See
[#303](https://github.com/discord/lilliput/issues/303).

## Reporting a vulnerability in this code

This repository is **not in scope** for Discord's security bug bounty program,
which covers sites and services Discord directly operates. Reports against this
code are not eligible for a bounty and will not receive a fix from us.

Because no fix is forthcoming, there is no value in embargoing findings here.
If you find a vulnerability in this code, please **open a public issue** so that
the forks and downstream users still running it can see it and act. You do not
need to contact us privately first, and we will not treat public disclosure of
an issue in this repository as irresponsible.

If you are running lilliput in production, note that it links a number of
third-party C libraries (see [third-party-licenses](third-party-licenses/)).
Vulnerabilities in those libraries should be reported upstream to their
respective projects; the vendored copies here will not be updated.

## Reporting a vulnerability in Discord

Vulnerabilities in Discord's own products and services remain in scope and are
unaffected by this policy. Report them per <https://discord.com/security>.
