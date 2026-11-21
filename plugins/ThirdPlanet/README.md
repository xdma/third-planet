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
Live III (MPC 3.10.1, 2026-11-20) it plays by itself when loaded and plays without clicks; the
pads-and-screen controls below are being checked on the device.

## Install

Copy `build/Shostak - VST - Third Planet/` into a folder named `VST2 Plugins` on an MPC drive, or
install it over the network with the SDK's `mpc_remote.py`. The MPC needs MPC OS 3.10.1 or later, and
external plugins must be on: Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin
Support.

## Playing it

Pads and keyboards only play; everything that switches or acts once is on the screen. Load the plugin on
a track and play the pads into it.

**Pad settings for the track** (the MPC's Pad Perform, on this track):
- Pad Perform on, mode **Chromatic** (not Notes), root **C**, scale **Chromatic**;
- octave set so that bank A pad 1 is **C1, note 36**;
- **Banks: the continuous option**, not "Start on root note" (that one makes the banks overlap:
  A 36–51, B 48–63, C 60–75).

Then bank A is notes 36–51, B 52–67 and C 68–83, and the three banks are Third Planet's:

| Notes (from Pad base, 36) | Bank | What a pad does while held |
| --- | --- | --- |
| 36–51 | A, Lead | The Lead plays scale degree 1–16 of the piece. Monophonic; a new pad while one is held plays legato. |
| 52–67 | B, Effects | The 16 effects of the FM-1's grid, in its order: Repeat, Time, Tape stop, Shimmer; Low-pass, High-pass, Sweep, Phaser; Reverb, Take-off, Freeze, Wash; Octave, Return, Twitter, Supernova. |
| 68–75 | C, Moves | Rise, Close, Transpose (the FM-1's black-key moves), then Sweep, Open, Shut, Drop, Echo (PERFORM's moves). |
| any other note | | The FM-1's MIDI in: the Lead, moved to the nearest note of the scale. |

**Keys** (PLAY page): Pads (the default) or Lead, where every note plays the Lead. **Pad base** moves the
three banks, for another pad layout or a controller. A pad held while Keys or Pad base changes is let go.

**Knobs.** Every FM-1 knob that sets a sound is an MPC parameter. The parameters are absolute, so a
Q-Link sets its value directly; on the FM-1 the knobs are relative and change meaning with the page. A
change goes through the same code the FM-1's knob runs. Values the FM-1 changes by itself come back to
the MPC's knobs: a new piece, Evolve's nudges, a shake, the engine moving the Filter, Wet and Energy
macros.

**Switches and one-shots.** What the FM-1 does with button gestures is on the screen:
- switches (two-way: the screen shows the FM-1's state, a tap sets it): Hold, Lock, Play, each layer's
  Mute and Solo, the Pattern's steps and accents;
- one-shots (a tap acts once, and the button goes back to Off): Forget, Next chord, New chords, Shake all,
  Reset piece, and each layer's Nudge and Shake.

Each calls the same code in the FM-1's app that its gesture calls.

**Status line.** Across the top of every page:
- **Piece**: the piece playing.
- **Chord**: the chord sounding, named as the FM-1's screen names it.
- **Pad**: what the last pad or note did ("Lead 3", "Shimmer", "Rise", "Note 90").
- **HOLD** and **LOCK**, while they are on.

The screen has eleven pages:

| Page | What is on it |
| --- | --- |
| PLAY | Hold, Lock, Play, Forget; Next chord, New chords, Shake all, Reset piece; Keys, Pad base, Sync |
| LAYERS | A column each for Drone, Seq, Lead, Seq 2 and Atmos: Mute, Solo, Nudge, Shake. Space: Nudge, Shake |
| MAIN | Filter, Evolve, Motion, Drift; Energy, Wet, Sweep, Volume; the four layer levels; Piece, Tempo, Root, Mode |
| DRONE | Pedal, Reso, Phaser, Attack; Colour, Release, Reverb, Echo; Width, Time, Rate, Intro; its voice |
| SEQ | Density, Octave, Length, Grid; Cutoff, Decay, Reverb, Echo; Repeat A/B and their times; its voice, Time, Rate, Intro |
| LEAD | Phrases, Octave, Glide, Vibrato; Tone, Attack, Reverb, Echo; Width, Time, Rate, Intro; its voice |
| SEQ 2 | Chance, Octave, Length, voice; Cutoff, Decay, Reverb, Echo; Width, Time, Rate, Intro |
| PATTERN | Lane, Accent, Lane chance, Pattern; the lane's 16 steps; their 16 accents (an accent turns its step on) |
| ATMOS | Density, Spread, Drift, Air; Colour, Ring, Reverb, Echo; Width, Time, Rate, Intro; its type and level |
| SPACE | Echo, Echo time, Feedback, Reverb; Blur, Tone, Width, Plate size; Lead in, Bridge, Arc, Memory; Transpose, Tune |
| CHORDS | Chord slot, Chord root ("-" is a gap), kind, type; Play (Endless or Loop), Walk |

The FM-1's centre notch and knob acceleration are left out, because the MPC's knobs are absolute.

**Sync.** Off (the default): the piece keeps its own Tempo and plays all the time, as on the FM-1. On:
Third Planet follows the MPC's tempo and transport, the way the FM-1 follows MIDI clock. The MPC's Play
starts the piece from the top, and Stop rests it.

**Saving.** The MPC project keeps the FM-1's own save, every piece as it was left, together with the
knob values. When a project loads, the FM-1 starts as from power-on with that save, and then the knob
values are applied. The switches the FM-1 never saves (Hold, Lock, Play, the mutes and solos) come back
from the MPC project too; one-shots never act from a load.

**One at a time.** The engine exists once per MPC. A second Third Planet stays silent, and its
status line says so, until the first is removed; then it takes over with its own settings.

**Not on the MPC:** the FM-1's screen (its pictures and help cards; the status line shows the main
facts instead), its pages and PERFORM as modes, the panel's gesture timing (taps and holds), MIDI out
(the MPC discards plugin MIDI out), the button lights, and the FM-1's own save gesture (GLO held). The
MPC project saves instead.

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
- `src/ThirdPlanet.cpp`, `src/params.h`: the MPC side (the pad banks, parameters, switches and
  one-shots, Sync, saving, the status line). The display parameters are read-only and come after the 173
  controls: the status line's four (Pad, Chord, Hold, Lock), then the star-chart screen's 56
  (`d_section` to `d_source`). They are sent to the MPC when they change, at most 24 notifications a
  block shared with the controls: the planets and drifts 10 times a second, the particles a half at a time. For half a second after a
  project load nothing of the display is sent, so the MPC keeps the values it loaded.
- `src/sky.{h,cpp}`: the star-chart screen's model (particles, planets, the log's three lines).
- `skins/`: the screen. `tools/gen_skin.py` makes it from `ui.json` with the SDK's generator, then adds
  the status line, moves the grid down, adds the PATTERN page's steps and accents and builds the LAYERS
  page. With `skins/` present the build copies it as it is, so rerun the script after changing `ui.json`
  or the parameters. On the MPC a label over a button takes its touches, so captions sit beside buttons.
- [docs/ui-behavior-map.md](docs/ui-behavior-map.md): what every FM-1 control does, traced from the code.

## Tests

From the repository root:

| Command | Checks |
| --- | --- |
| `plugins/ThirdPlanet/tests/null_test.sh` | The engine as built here (C++) against upstream's build (C99, `-ffp-contract=off`). All ten pieces must match bit for bit. |
| `plugins/ThirdPlanet/tests/app_null.sh` | The app and the engine against upstream's own host simulator (`host/amb_host.c`, unpatched sources), on scripted play (keys, effects, pieces, knobs, PERFORM, Hold, Evolve). Every WAV must match byte for byte. |
| `plugins/ThirdPlanet/tests/link_null.sh` | The default path against the link baseline (`e58a6f3`, before Harmony Link): one script of pads, notes and CCs on all channels, Performer's panic, parameter writes and a transport (starts mid-bar included), run through the plugin built three ways (that commit, `TP_LINK=0`, `TP_LINK=1` with Link Off). Audio, saved project and final values must be identical; without Link every value sent to the MPC too, with it each control's values in the same order. The parameter list must equal that commit's (with Link: plus exactly "Link", after Sync), and the skin without Link must be that commit's, byte for byte. |
| `plugins/ThirdPlanet/tests/all.sh` | Builds and runs every C++ test below twice, without Harmony Link (`TP_LINK=0`) and with it (`TP_LINK=1`). |
| `c++ -std=c++17 -O2 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework plugins/ThirdPlanet/tests/test_plugin.cpp plugins/ThirdPlanet/src/app_unit.cpp plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp plugins/ThirdPlanet/src/sky.cpp -o plugins/ThirdPlanet/build/tests/test_plugin && plugins/ThirdPlanet/build/tests/test_plugin` | The plugin glue. A reboot must be a power-on. Output must equal the app's own at any block size, with and without pads played. Then parameters both ways, a save and a load, and a second instance. |
| `tests/test_controls.cpp` (built like `test_plugin`) | The controls: every pad bank against the engine state it sets, releases across a change of Keys or Pad base, all notes off, notes outside the banks; every switch both ways; every one-shot acts once a press and goes back to Off; steps, accents and the chord root; a project load that fires no one-shot. |
| `tests/test_equiv.cpp` (built like `test_plugin`) | Every MPC control against the FM-1 gesture it stands for, on two bit-exact boots: PERFORM's mute, solo and move keys, REC held with a key, the black keys, HOME and GLO held on, the nudge and shake holds, the Pattern and Chords pages' keys. Audio and saved project must be identical. |
| `tests/test_display.cpp` (built like `test_plugin`) | The status line. The chord's name must equal the FM-1 screen's own (`chord_now`) on every piece, walk and Loop setting. Pad, Hold and Lock must follow the pads and the switches. A value the MPC writes over must be corrected, and a second instance must show only its message. The screen's display parameters: each follows the state it shows, no block sends more than 24 notifications, nothing is sent at rest, a project load logs nothing and holds the display for half a second, the drift texts follow the engine, and every text fits 31 bytes. |
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
