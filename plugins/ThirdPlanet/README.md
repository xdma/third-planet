# Third Planet

A generative 1970s Berlin School instrument for Akai MPC standalone hardware (target: MPC Live 3,
MPC OS 3.10.1+), by Shostak. It is a port of **NACHTFAHRT** by Cranky Man
(<https://github.com/FartyPants/fm1-nachtfahrt>), the firmware for the M-VAVE FM-1. The port uses
NACHTFAHRT's own code, unchanged, for both the sound and the behaviour:
- its engine (two drifting sequences, a drone, a gliding Lead with a memory, an Atmosphere, the echo
  and the plate);
- its whole app: the pages, what every button and key does, PERFORM, the Chords and Pattern pages, the
  ten pieces, Evolve's slow changes.

It plays by itself from the moment it is loaded, as the FM-1 does from power-on.

Status: **in development** (0.1, not released). It builds and passes the tests below. On an MPC
Live III (MPC 3.10.1, 2026-11-20) it plays by itself when loaded, the pads reach the keys and
buttons, all eight pages show, and it plays without clicks.

## Install

Copy `build/Shostak - VST - Third Planet/` into a folder named `VST2 Plugins` on an MPC drive, or
install it over the network with the SDK's `mpc_remote.py`. The MPC needs MPC OS 3.10.1 or later, and
external plugins must be on: Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin
Support.

## Playing it

The plugin is the FM-1's panel made of MPC controls. Load it on a track and play pads or a keyboard into
it.

Note numbers are given first. The names use middle C = C4 (note 60); the MPC may label note 60 C3,
and then every name below shows one octave lower there.

| MIDI notes | What they are on the FM-1 |
| --- | --- |
| 53..79 (F3..G5) | The 27 keys. White keys play the Lead in the piece's scale; black keys hold the 11 effects (Take-off, Supernova, Twitter, Repeat, Shimmer, Freeze, Octave, Return, Rise, Close, Transpose). In PERFORM, on the Pattern page and on the Chords page they do what they do there. |
| 36..49 (C2..C#3) | The 14 buttons, held for as long as the note is held: 36 FX, 37 SEL, 38 ENV, 39 LFO, 40 EDIT, 41 GLO, 42 HOME, 43 SAVE, 44 ARP, 45 SEQ, 46 PLAY, 47 REC, 48 OCT-, 49 OCT+. Taps and holds work as on the FM-1: REC tapped = Hold, REC held = the effects grid on the white keys, SEL = PERFORM, EDIT = the Chords page or the next chord, ENV/ARP/LFO/SEQ/FX/SAVE held = a nudge, then a shake. |
| any other note | The FM-1's MIDI in: the Lead, moved to the nearest note of the scale. |

With **Keys** set to Lead, every note plays the Lead and none act as keys or buttons.

**Knobs.** Every FM-1 knob that sets a sound is an MPC parameter. The parameters are absolute, so a
Q-Link sets its value directly; on the FM-1 the knobs are relative and change meaning with the page. A
change goes through the same code the FM-1's knob runs. Values the FM-1 changes by itself come back to
the MPC's knobs: a new piece, Evolve's nudges, a shake, the engine moving the Filter, Wet and Energy
macros.

**Status line.** Across the top of every page:
- **Piece**: the piece playing.
- **Chord**: the chord sounding, named as the FM-1's screen names it.
- **Page**: the FM-1 page the keys and buttons act on now. (The MPC's knobs are absolute and do not
  follow it.)
- **PERFORM** (and **LOCK**) and **HOLD**, while they are on.

The buttons are pads, so this is how you see what the keys do at the moment.

The screen has eight pages, laid out like the FM-1's:

| Page | What is on it |
| --- | --- |
| MAIN | Filter, Evolve, Motion, Drift; Energy, Wet, Sweep, Volume; the four layer levels; Piece, Tempo, Root, Mode |
| DRONE | Pedal, Reso, Phaser, Attack; Colour, Release, Reverb, Echo; Width, Time, Rate, Intro; its voice |
| SEQ | Density, Octave, Length, Grid; Cutoff, Decay, Reverb, Echo; Repeat A/B and their times; its voice, Time, Rate, Intro |
| LEAD | Phrases, Octave, Glide, Vibrato; Tone, Attack, Reverb, Echo; Width, Time, Rate, Intro; its voice |
| SEQ 2 | Chance, Octave, Length, voice; Cutoff, Decay, Reverb, Echo; the Pattern page (Lane, Accent, Lane chance, Pattern); Width, Time, Rate, Intro |
| ATMOS | Density, Spread, Drift, Air; Colour, Ring, Reverb, Echo; Width, Time, Rate, Intro; its type and level |
| SPACE | Echo, Echo time, Feedback, Reverb; Blur, Tone, Width, Plate size; Lead in, Bridge, Arc, Memory; Transpose, Tune, Sync, Keys |
| CHORDS | Chord slot, kind, type, Play (Endless or Loop), Walk |

The FM-1's centre notch and knob acceleration are left out, because the MPC's knobs are absolute.

**Sync.** Off (the default): the piece keeps its own Tempo and plays all the time, as on the FM-1. On:
Third Planet follows the MPC's tempo and transport, the way the FM-1 follows MIDI clock. The MPC's Play
starts the piece from the top, and Stop rests it.

**Saving.** The MPC project keeps the FM-1's own save, every piece as it was left, together with the
knob values. When a project loads, the FM-1 starts as from power-on with that save, and then the knob
values are applied.

**One at a time.** The engine exists once per MPC. A second Third Planet stays silent, and its
status line says so, until the first is removed; then it takes over with its own settings.

**Not on the MPC:** the FM-1's screen (its pictures and help cards; the status line shows the main
facts instead), MIDI out (the MPC discards plugin
MIDI out), the button lights, and the FM-1's own save gesture (GLO held). The MPC project saves
instead.

## How it is built

- `src/nachtfahrt/`: NACHTFAHRT's code at upstream commit `f2de0d6`. The engine is `dsp/`, the app is
  `app/ui.c` and `app/project.c`, and the drawing code is `gfx.c`. Three small patches let it compile as
  C++; [docs/VENDOR.md](docs/VENDOR.md) lists them, with the reasons.
- `src/engine_unit.cpp`: the engine, built as upstream builds it (no fused multiply-add).
- `src/app_unit.cpp`: the app on a virtual FM-1, the way upstream's own host simulator runs it. Time
  is counted in rendered audio: every simulated millisecond renders the 256-frame block that falls due,
  then runs the UI.
- `src/power.{h,cpp}`: the power switch. Every NACHTFAHRT variable goes back to its power-on value on
  every boot.
- `src/ThirdPlanet.cpp`, `src/params.h`: the MPC side (notes to keys and buttons, parameters, Sync,
  saving, the status line). The status line is four read-only parameters after the 109 controls (Page,
  Chord, Perform, Hold), sent to the MPC when they change.
- `skins/`: the screen. `tools/gen_skin.py` makes it from `ui.json` with the SDK's generator, then adds
  the status line and moves the grid down. With `skins/` present the build copies it as it is, so rerun
  the script after changing `ui.json` or the parameters.
- [docs/ui-behavior-map.md](docs/ui-behavior-map.md): what every FM-1 control does, traced from the code.

## Tests

From the repository root:

| Command | Checks |
| --- | --- |
| `plugins/ThirdPlanet/tests/null_test.sh` | The engine as built here (C++) against upstream's build (C99, `-ffp-contract=off`). All ten pieces must match bit for bit. |
| `plugins/ThirdPlanet/tests/app_null.sh` | The app and the engine against upstream's own host simulator (`host/amb_host.c`, unpatched sources), on scripted play (keys, effects, pieces, knobs, PERFORM, Hold, Evolve). Every WAV must match byte for byte. |
| `c++ -std=c++17 -O2 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework plugins/ThirdPlanet/tests/test_plugin.cpp plugins/ThirdPlanet/src/app_unit.cpp plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp -o plugins/ThirdPlanet/build/tests/test_plugin && plugins/ThirdPlanet/build/tests/test_plugin` | The plugin glue. A reboot must be a power-on. Output must equal the app's own at any block size. Then notes, parameters both ways, a save and a load, and a second instance. |
| `tests/test_display.cpp` (built like `test_plugin`) | The status line. The chord's name must equal the FM-1 screen's own (`chord_now`) on every piece, walk and Loop setting. Page, PERFORM and Hold must follow taps on the button notes. A value the MPC writes over must be corrected, and a second instance must show only its message. |
| `tests/test_sync.cpp` (built like `test_plugin`) | Sync, through the plugin's VST entry point and a fake host clock: rest, start, the tempo followed at 120 and 90 BPM, stop, and Sync off while playing and while resting. |
| `tests/test_fuzz.cpp` (built like `test_plugin`, with `-fsanitize=address,undefined -fno-sanitize=shift-base`) | Random notes, all-notes-off, parameters, saves and loads, at random block sizes. The output must stay finite and bounded. Upstream's `wd_read` reads one sample past its buffer (T-1723); run it with `-fsanitize-recover=address ASAN_OPTIONS=halt_on_error=0` until that is decided. |
| `deps/mpc-plugin-sdk/.sdk-tools/host_sim plugins/ThirdPlanet/build/native/third-planet.dylib test` | The SDK's MPC host checks. Two warnings are expected: the instrument plays on after all-notes-off, and the saved project changes as it plays. |

CPU: about 0.4 % of one desktop core for the engine, the app and its drawing. Not yet measured on the MPC.

## Licence and credits

Third Planet is free software under the GNU General Public License, version 3 only (GPL-3.0-only),
because it is built on NACHTFAHRT, which is GPL-3.0-only. See `src/nachtfahrt/LICENSE`.

- **NACHTFAHRT** by Cranky Man: the concept, the compositions, the engine and the app.
- NACHTFAHRT is a fork of [DUB-1](https://github.com/sbegueria/fm1-dub), itself a fork of
  [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal.
- It draws on [Felucca](https://github.com/hugelton/Felucca) and [Skiff](https://github.com/illuminus5/skiff)
  (Leo Kuroshita, Hügelton Instruments), [fm1_open](https://github.com/m-vave/fm1_open) (M-VAVE; FM voices,
  Pluck after DaisySP, the SEA codec), [JIANT](https://github.com/juanjiant-bit/JIANT), and Jon Dattorro's
  plate reverb.
- The piano samples are from the VCSL Kawai grand (CC0).

`src/nachtfahrt/LICENSING.md` has the full origins table. Third Planet is not affiliated with Cranky
Man, M-VAVE or Hügelton Instruments.
