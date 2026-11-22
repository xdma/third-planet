# Contributing to Third Planet

Bug reports, fixes and improvements are welcome. Open an issue first for anything large.

## Repository layout

- `plugins/ThirdPlanet/` — the plugin: `plugin.json`, `ui.json`, `src/`, `skins/`, `tests/`, `tools/`, `docs/`.
- `deps/mpc-plugin-sdk/` — the official [MPC Plugin SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk),
  a git submodule pinned to a commit. Do not edit it; update it by moving the pin.
- `release/` — the packaged release zips (each is also attached to its GitHub release).
- `docs/` — the user guide and the Harmony Link overview.

The SDK's docs are written as if you work inside the SDK folder: read their `tools/…`, `framework/…`, `docs/…`
paths as `deps/mpc-plugin-sdk/tools/…` and so on.

## Rules

- **The vendored NACHTFAHRT code in `plugins/ThirdPlanet/src/nachtfahrt/` stays as upstream wrote it.** The only
  changes are the small patches listed in [`plugins/ThirdPlanet/docs/VENDOR.md`](plugins/ThirdPlanet/docs/VENDOR.md).
  Sound and behaviour changes belong upstream, in [NACHTFAHRT](https://github.com/FartyPants/fm1-nachtfahrt).
  `null_test.sh` and `app_null.sh` enforce this bit for bit.
- **The plugin identity is fixed:** manufacturer `Shostak`, name `Third Planet`, uid `Ssni`. Changing any of them
  makes the MPC treat it as a different plugin and breaks saved projects.
- The audio thread allocates nothing, takes no locks and does no I/O.
- Licence: GPL-3.0-only. New files carry `// SPDX-License-Identifier: GPL-3.0-only`.

## Build

You need Python 3.9+ and a C++17 compiler for the host tests. The SDK's build script downloads the Zig
cross compiler for the MPC's ARM targets on first use (about 50 MB).

```bash
git clone --recurse-submodules <this repository>
cd third-planet
python3 deps/mpc-plugin-sdk/tools/build.py plugins/ThirdPlanet
```

The output is `plugins/ThirdPlanet/build/Shostak - VST - Third Planet/` (native, 32-bit ARM and 64-bit ARM).

The default build has Harmony Link compiled out (`TP_LINK=0`). To build it in, use
`plugins/ThirdPlanet/tools/build_variant.sh on` (it leaves the committed skin as it is); see [docs/HARMONY-LINK.md](docs/HARMONY-LINK.md).
The committed `skins/` is the default build's skin.

**The skin.** `plugins/ThirdPlanet/tools/gen_skin.py` generates `skins/` from `ui.json` and the built
`params.json`. With `skins/` present the build copies it as it is, so after changing `ui.json` or the parameters,
build, run `python3 plugins/ThirdPlanet/tools/gen_skin.py`, and build again (`build_variant.sh off` does all three).

## Release

```bash
plugins/ThirdPlanet/tools/build_variant.sh off
python3 deps/mpc-plugin-sdk/tools/package.py plugins/ThirdPlanet --out release
```

Bump `version` in `plugins/ThirdPlanet/plugin.json` first. Test the zip on an MPC before publishing. Then commit
the zip, tag the commit `v<version>` and attach the zip to a GitHub release:

```bash
gh release create v<version> release/third-planet-<version>-mpc.zip --title "Third Planet <version>"
```

## How it is built

- `src/nachtfahrt/`: NACHTFAHRT's code at upstream commit `f2de0d6`. The engine is `dsp/`, the app is
  `app/ui.c` and `app/project.c`, and the drawing code is `gfx.c`. Three small patches let it compile as
  C++; [docs/VENDOR.md](plugins/ThirdPlanet/docs/VENDOR.md) lists them, with the reasons.
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
- [docs/ui-behavior-map.md](plugins/ThirdPlanet/docs/ui-behavior-map.md): what every FM-1 control does, traced from the code.

## Tests

Run from the repository root. All of them must pass before a change is merged.


| Command | Checks |
| --- | --- |
| `plugins/ThirdPlanet/tests/null_test.sh` | The engine as built here (C++) against upstream's build (C99, `-ffp-contract=off`). All ten pieces must match bit for bit. |
| `plugins/ThirdPlanet/tests/app_null.sh` | The app and the engine against upstream's own host simulator (`host/amb_host.c`, unpatched sources), on scripted play (keys, effects, pieces, knobs, PERFORM, Hold, Evolve). Every WAV must match byte for byte. |
| `plugins/ThirdPlanet/tests/link_null.sh` | The default path against the link baseline (`d31f2a8`, before Harmony Link): one script of pads, notes and CCs on all channels, a sequencer's panic, parameter writes and a transport (starts mid-bar included), run through the plugin built three ways (that commit, `TP_LINK=0`, `TP_LINK=1` with Link Off). Audio, saved project and final values must be identical; without Link every value sent to the MPC too, with it each control's values in the same order. The parameter list must equal that commit's (with Link: plus exactly "Link", after Sync), and the skin without Link must be that commit's, byte for byte. |
| `plugins/ThirdPlanet/tests/all.sh` | Builds and runs every C++ test below twice, without Harmony Link (`TP_LINK=0`) and with it (`TP_LINK=1`). |
| `c++ -std=c++17 -O2 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework plugins/ThirdPlanet/tests/test_plugin.cpp plugins/ThirdPlanet/src/app_unit.cpp plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp plugins/ThirdPlanet/src/sky.cpp plugins/ThirdPlanet/src/link_codec.cpp plugins/ThirdPlanet/src/link_capture.cpp plugins/ThirdPlanet/src/link_alsa.cpp plugins/ThirdPlanet/src/link_worker.cpp -o plugins/ThirdPlanet/build/tests/test_plugin && plugins/ThirdPlanet/build/tests/test_plugin` | The plugin glue. A reboot must be a power-on. Output must equal the app's own at any block size, with and without pads played. Then parameters both ways, a save and a load, and a second instance. |
| `tests/test_controls.cpp` (built like `test_plugin`) | The controls: every pad bank against the engine state it sets, releases across a change of Keys or Pad base, all notes off, notes outside the banks; every switch both ways; every one-shot acts once a press and goes back to Off; steps, accents and the chord root; a project load that fires no one-shot. |
| `tests/test_equiv.cpp` (built like `test_plugin`) | Every MPC control against the FM-1 gesture it stands for, on two bit-exact boots: PERFORM's mute, solo and move keys, REC held with a key, the black keys, HOME and GLO held on, the nudge and shake holds, the Pattern and Chords pages' keys. Audio and saved project must be identical. |
| `tests/test_display.cpp` (built like `test_plugin`) | The status line. The chord's name must equal the FM-1 screen's own (`chord_now`) on every piece, walk and Loop setting. Pad, Hold and Lock must follow the pads and the switches. A value the MPC writes over must be corrected, and a second instance must show only its message. The screen's display parameters: each follows the state it shows, no block sends more than 24 notifications, nothing is sent at rest, a project load logs nothing and holds the display for half a second, the drift texts follow the engine, and every text fits 31 bytes. |
| `tests/test_sync.cpp` (built like `test_plugin`) | Sync, through the plugin's VST entry point and a fake host clock: rest, start, the tempo followed at 120 and 90 BPM, stop, and Sync off while playing and while resting; a start mid-bar waits for the bar line. Eight minutes of a 2-bar MPC loop, for each way the host's position may wrap (blocks of 128 and 512), must leave the engine within a sixteenth of the host, and a locate back must neither burst nor stall the clock. Takes about 20 s. |
| `tests/test_fuzz.cpp` (built like `test_plugin`, with `-fsanitize=address,undefined -fno-sanitize=shift-base`) | Random notes, all-notes-off, parameters, saves and loads, at random block sizes. The output must stay finite and bounded. Upstream's `wd_read` reads one sample past its buffer ; run it with `-fsanitize-recover=address ASAN_OPTIONS=halt_on_error=0` until that is decided. |
| `tests/test_link_codec.cpp` (`TP_LINK=1` only; built alone with `src/link_codec.cpp`, under ASan and UBSan; run from the repository root) | Harmony Link v1's wire codec against the golden vectors of `plugins/ThirdPlanet/docs/harmony-link-vectors.txt`, read at run time (the format: `plugins/ThirdPlanet/docs/harmony-link.md`). PLAN, EVENT and BYE must encode to the vectors' bytes, every byte of 1000 random PLANs must be 7-bit, FADE and HOLD must decode, and malformed commands (truncated, another version or manufacturer, a byte over 7F, a PLAN, no F7, too long) must be rejected. |
| `tests/test_link.cpp` (`TP_LINK=1` only; built like `test_plugin`, with `-DTP_LINK=1`) | Harmony Link's capture on the audio thread, draining the plugin's ring directly. Link Off must push nothing; Link On must push a Plan when it turns on and then only on a change. A hand's Next chord must come out as an Event, then a Plan with the new chord; Evolve's values must not be forwarded. The Plan must carry the readouts and the host's position in 16ths, written slots' masks the engine's own voicing. The ring must keep Events in push order (the newest dropped when full) and never lose the latest Plan. With Link On the SKY log and the audio must be what they are with Link Off. The plugin's own link thread must take the ring with Link On and idle with Link Off (every other test here keeps it idle). The commands: FADE over the host's bars, at once, reset by the MPC's Play and by a project load; HOLD through the hand's setter, released at Play only if a command set it, and never sent back as an Event (the hand's own Hold still is). |
| `tests/test_link_worker.cpp` (`TP_LINK=1` only; built alone with `src/link_worker.cpp`, `src/link_alsa.cpp` and `src/link_codec.cpp`, under ASan and UBSan) | Harmony Link's thread against a fake ALSA sequencer and a clock the test steps. A Plan must go out within one poll, Plans at most 20 a second with the last one always sent, Plans that differ only in the layers' activity, densityShift and pos16 at most 2 a second (any other change, and a Plan after an Event, at once), the last Plan again every 2 s, Events at once and in order. A send the sequencer refuses (busy) must lose an Event, counted, and keep a Plan for the next poll. Stopping and Link Off must send BYE last and close. The out port must be readable, and the in port writable, by its owner only. With the USB MIDI function absent nothing may be sent, and it must be looked for every 2 s; a send error, or the function gone silently, must give a new client and connection, no more often than every 2 s. FADE and HOLD must be read whole from pieces, and malformed or oversized messages ignored. Items left in the ring must not be sent when the link opens. 200 starts and stops, and 400 flips of Link on a running thread while the ring is being pushed, must not hang, must close every open and must end with a PLAN after the last On; with no libasound the ring must still be drained. |
| `deps/mpc-plugin-sdk/.sdk-tools/host_sim plugins/ThirdPlanet/build/native/third-planet.dylib test` | The SDK's MPC host checks. Two warnings are expected: the instrument plays on after all-notes-off, and the saved project changes as it plays. |

CPU: about 0.4 % of one desktop core for the engine, the app and its drawing. Not yet measured on the MPC.
