# Vendored code: NACHTFAHRT

Third Planet's sound engine is NACHTFAHRT's, by Cranky Man, taken without changes wherever possible.

- Upstream: <https://github.com/FartyPants/fm1-nachtfahrt>
- Pin: `f2de0d6f884d02ef29be58c010056224ef5dd98d` (2026-11-15)
- Licence: GPL-3.0-only (see `src/nachtfahrt/LICENSE` and `src/nachtfahrt/LICENSING.md` for every
  origin and licence inside the engine). Because Third Planet links this code, Third Planet is
  GPL-3.0-only as well.

## Files

`src/nachtfahrt/` mirrors upstream's `firmware/src/` layout at the pin:
- the engine in `dsp/`, from `firmware/src/dsp/`;
- the app in `app/` (`ui.c`, `project.c`, `app.h`, `plat.h`), from `firmware/src/app/`;
- `gfx.c`.

Every file was verified byte-identical against the upstream git blob hash when it was copied. The
tables below list the files before the patches.

| App file | Role |
| --- | --- |
| `app/ui.c` | the FM-1's whole behaviour (and its screen) |
| `app/project.c` | the saved project, the pieces' slots |
| `app/app.h`, `app/plat.h` | the app's types and its platform interface |
| `gfx.c` | the screen's drawing helpers (Felucca's) |

Added by Third Planet:
- `src/felucca_font.h` stands in for upstream's generated font (`tools/gen_font.py` needs Pillow and the
  TTFs). It has the same five fonts and the same struct, with every glyph blank. The MPC never shows
  the FM-1's screen.
- `src/app_unit.cpp` replaces the screen reader (`announce.c`) with stubs: no reader is attached, as on
  an FM-1 without one.

| File | Role |
| --- | --- |
| `amb.c` | the engine. It `#include`s `seq.c`, `fm.c`, `rainloop.c` and `pianosamp.c`, so it is one translation unit |
| `amb.h` | the engine API (`en_*`) |
| `seq.c`, `seq.h` | the sequences' patterns |
| `fm.c` | the FM voices |
| `fastmath.h` | the approximations the engine uses |
| `rainloop.c` | the Stereo Rain recording (SEA5-coded) |
| `pianosamp.c` | three piano notes (VCSL, CC0; SEA5-coded) |
| `LICENSE`, `LICENSING.md` | upstream's licence and origins table |

`tests/upstream/` holds reference copies that are never built into the plugin: `host/en_render.c`,
`host/amb_host.c` and `host/build_host.sh` (used by the null test), the app layer
`firmware/src/app/{ui.c,project.c,main_fm1.c,app.h,plat.h}` (the source of the behaviour port), and
`docs/MANUAL.md`.

Upstream builds the engine with `-O2 -ffp-contract=off -std=c99 -DEN_HOST`.

## Patches

The engine is compiled through `src/engine_unit.cpp`. Every change made to a file in
`src/nachtfahrt/` is listed here, with its reason.

1. `amb.c`: `fx_swm` was a C tentative definition at line 1578 (`static float fx_swm;`) that was
   defined again at line 5450 (`fx_swm = 1.0f`). C++ rejects the redefinition. The initialiser moved
   to line 1578 (`static float fx_swm = 1.0f;`) and the second definition was dropped. The behaviour
   is identical: the variable is one zero-cost static that starts at 1.0f either way.

2. `app/ui.c`, at the knob row's brightness arc (about line 2616): `&(int){0}, &(float){0.0f}` are C
   compound literals, which C++ does not have. Two named locals (`bo_i`, `bo_f`) take their place.
   This is drawing code only.
3. `app/ui.c`: `sp_buf` and `sp_n` were C tentative definitions at line 249 and were defined again at
   about line 2729. The second pair was dropped.

The behaviour is unchanged: `tests/app_null.sh` builds upstream's host simulator from the unpatched
`ui.c` (kept in `tests/upstream/orig/ui.c`) and compares the two byte for byte.

Every variable in these files is placed in Third Planet's own sections (`TP_SECTIONS_BEGIN` in
`engine_unit.cpp` and `app_unit.cpp`, see `src/power.h`). That way a boot restores the power-on state.
This needs no change to the files.

With patch 1 the whole engine compiles as C++17 (`clang++ -Wall`: no errors, no warnings)
for native, arm32 and arm64.

C/C++ differences we checked:
- **Math overloads.** The engine calls no libm function: `<math.h>` is never included, and every
  `sin`, `exp`, `tan` and so on is upstream's own `fm_*` from `fastmath.h`. C++'s float overloads
  therefore cannot change any result.
- **Fused multiply-add.** `#pragma clang fp contract(off)` in `engine_unit.cpp` reproduces upstream's
  `-ffp-contract=off`.

## EN_HOST audit

`engine_unit.cpp` defines `EN_HOST`, as upstream's host simulator and browser emulator builds do. All
27 `#ifdef EN_HOST` blocks in `amb.c` and `fm.c` were read:
- **Test readouts.** Most only add test counters and probes: `en_ks_struck`, `en_lead_*`,
  `en_ch_work`, and the `*_ok` state checks.
- **Pool placement.** `EN_POOL` loses the device's `.pool` RAM section.
- **Test hooks with neutral defaults.**
  - `shm_k *= en_shm_ret`, where `en_shm_ret` is 1.0f. Multiplying by 1.0f is exact.
  - `WN_PD` and `WN_PW` come from `en_wn_part`, which starts at 0, so both are 1.0f.

None of them changes the sound. `en_lead_motif` and `en_par_get` exist only under `EN_HOST`, and
Third Planet uses them to restore state.

## Updating

Move the pin and copy the same files again (and `firmware/src/app/ui.c` unpatched into `tests/upstream/orig/`). Check their blob hashes with
`gh api repos/FartyPants/fm1-nachtfahrt/git/trees/<sha>?recursive=1` against `git hash-object`.
Re-apply the patches above, then rerun `tests/null_test.sh`, `tests/app_null.sh` and `tests/test_plugin.cpp`.
