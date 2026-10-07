# Hyprune core

Hyprland integration and the world runtime. **Early, building in the open.**

> What if the computer itself were an explorable world?

Core owns the small Hyprland plugin, `hypruned` helper daemon, scene/runtime, rendering, window capture, trusted input routing, capability enforcement and authoritative state. It is the only repository that depends on Hyprland internals.

The plugin draws prepared resources in Hyprland's render pass. Cursor/focus/workspace changes, capture snapshots and other potentially reentrant operations run only after the outer render pass has unwound. The daemon handles IPC, package validation, persistence and process supervision. See [RFC-0002](https://hyprune.com/rfcs/0002-runtime/).

Core excludes QML shell UI, official world art, SDK tooling and third-party native plugins. Worlds are data; shell and extensions are clients. Keep a compositor-owned emergency exit independent of either.

Implementation is being brought up separately. This foundation change adds documentation and licensing only; it does not provide or claim a working runtime.

## Boundaries and contracts

- [RFC-0001: umbrella architecture](https://hyprune.com/rfcs/0001-umbrella/)
- [RFC-0003: IPC `0.1`](https://hyprune.com/rfcs/0003-ipc/)
- [RFC-0004: world format `0.1`](https://hyprune.com/rfcs/0004-world-format/)
- [Machine-readable schemas and fixtures](https://github.com/hyprune/schema)
- [Roadmap M0–M3](https://hyprune.com/roadmap/)

RFCs are proposed implementation targets pending owner review. Read them now in [the docs source](https://github.com/hyprune/docs/tree/main/src/content/docs/rfcs) if the site is not deployed. Each core release must publish its exact Hyprland compatibility target and supported contract versions. Hyprune is new code; Hypr3D is a lessons-only historical reference.

## Contributing

See the [organization guide](https://github.com/hyprune/.github/blob/main/CONTRIBUTING.md) and [Code of Conduct](https://github.com/hyprune/.github/blob/main/CODE_OF_CONDUCT.md). Contract changes begin with an RFC amendment and schema fixtures. Keep provenance and failure evidence with your PR.

License: MIT; see [LICENSE](LICENSE).
