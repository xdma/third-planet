# Third Planet — notes for coding agents

Third Planet is a VST2 instrument for Akai MPC standalone (MPC Live III, MPC OS 3.10.1+), built with the
official MPC Plugin SDK. It is a port of NACHTFAHRT (FM-1 firmware by Cranky Man) and runs its engine and app
code unchanged. Read [CONTRIBUTING.md](CONTRIBUTING.md) first: layout, rules, build, tests.

## Paths

- `plugins/ThirdPlanet/` — the plugin. `deps/mpc-plugin-sdk/` — the SDK, a pinned git submodule: never edit it.
- The SDK's docs and skills are written as if you work inside the SDK folder. Translate their paths:
  `tools/…`, `framework/…`, `docs/…`, `examples/…`, `templates/…`, `ui-kit/…`, `schemas/…`, `.sdk-tools/…` →
  `deps/mpc-plugin-sdk/…`; `plugins/<Name>` stays `plugins/<Name>`.
- The SDK's `mpc-plugin-*` skills are symlinked into `.claude/skills/`. Read `deps/mpc-plugin-sdk/AGENTS.md`
  for the SDK's own rules.

## Commands (from the repository root)

- Build: `python3 deps/mpc-plugin-sdk/tools/build.py plugins/ThirdPlanet`
- Build with the skin regenerated: `plugins/ThirdPlanet/tools/build_variant.sh off` (default, no Harmony Link)
  or `on` (with Harmony Link; rewrites `skins/`, so run `off` again before committing).
- Tests: `plugins/ThirdPlanet/tests/null_test.sh`, `app_null.sh`, `link_null.sh`, `all.sh`, then
  `deps/mpc-plugin-sdk/.sdk-tools/host_sim plugins/ThirdPlanet/build/native/third-planet.dylib test`.
- Package: `python3 deps/mpc-plugin-sdk/tools/package.py plugins/ThirdPlanet --out release`

## Rules

- `plugins/ThirdPlanet/src/nachtfahrt/` is vendored upstream code: do not change it beyond the patches in
  `plugins/ThirdPlanet/docs/VENDOR.md`. The null tests must stay bit-exact.
- The plugin identity is fixed: manufacturer `Shostak`, name `Third Planet`, uid `Ssni`.
- `TP_LINK` (Harmony Link) is off by default. With it off, the plugin must equal the pre-Link baseline
  (`tests/link_null.sh`); keep it that way.
- Audio-thread code: no allocation, no locks, no I/O.
- Never run `deps/mpc-plugin-sdk/tools/mpc_firmware.py`, and never modify MPC system files.
- Licence GPL-3.0-only; new source files carry an SPDX header.
