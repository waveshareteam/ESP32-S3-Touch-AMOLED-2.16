# Continuous Integration

[简体中文](ci_ZH.md) · [Home](../README.md)

The always-visible `lightweight-gate` classifies a complete base/head diff,
runs the repository-local documentation policy and stdlib synthetic tests, and
publishes the selected matrices. It fails rather than guessing when the diff is
empty or its base is unavailable.

ESP-IDF discovery covers the five direct projects in `examples/esp-idf/` with
`v5.5.5` and `v6.0.2`. Arduino discovery covers the seven direct sketches in
`examples/arduino/` with core `3.3.11`; `examples/arduino/libraries/**` is not a
product sketch. `workflow_dispatch` accepts `all`, a project/sketch name, or a
repository-relative example path.

Markdown at the root, inside an example, beside a sketch, or in a bundled
library gets the lightweight gate only. Direct example code selects that one
example; shared/global inputs select the applicable full matrix. Changes under
`firmware/` are reported but never enter the example matrix. Unknown complete
paths conservatively select all examples and remain visible.

For maintainer audits, run the documented Waveshare inventory, Markdown, and
routing tools with `config/markdown-audit.json` and `config/ci-routing.json`.
The repository-local policy script is deliberately a limited companion check,
not a replacement for that full audit.
