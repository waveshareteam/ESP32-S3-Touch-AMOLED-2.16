# Repository structure

[简体中文](repository-structure_ZH.md) · [Home](../README.md)

This is an already-normalized single-product repository. First-party source is
kept in `examples/esp-idf/` and `examples/arduino/`; bundled Arduino libraries
and the embedded Brookesia upstream tree remain outside product-example scope.

| Path | Purpose |
| --- | --- |
| `examples/esp-idf/` | Five direct first-party ESP-IDF projects |
| `examples/arduino/` | Seven direct Arduino sketches plus bundled libraries |
| `firmware/` | Immutable factory recovery delivery |
| `config/` | Shared configuration and audit policy |
| `docs/` | First-party customer and maintainer notes |
| `releases/` | Example artifact packaging/download helpers |

No versioned example roots, missing template directories, or local reusable BSP
copy migration is pending in this layout. See [CI](ci.md) for selected-build
routing and [components](components.md) for retained local code.
