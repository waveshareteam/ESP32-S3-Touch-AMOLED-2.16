# Contributing

[简体中文](CONTRIBUTING_ZH.md)

Keep contributions focused, avoid generated build outputs, and update the
matching documentation when paths, CI behavior, or delivery artifacts change.
Open an issue before behavioral changes or larger maintenance work, then use a
topic branch and complete the pull-request template.

## Validation

Example build validation is performed by GitHub Actions. The lightweight gate
always runs documentation policy, discovery, and routing tests; example builds
run only when the complete diff selects them. Do not include host-specific paths,
user names, device identifiers, or tool provenance in public text.
