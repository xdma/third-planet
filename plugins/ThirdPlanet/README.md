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

Full user guide, source code and updates: <https://github.com/xdma/third-planet>.

## Install

Copy the folder `Shostak - VST - Third Planet` into a folder named exactly `VST2 Plugins` at the top level of
the MPC's internal drive (create it if needed). The MPC needs MPC OS 3.10.1 or later, and external plugins must
be on: Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin Support. Then create a Plugin
program and pick **Third Planet** under External Plugins. INSTALL.txt has the details.

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
| SKY | The star chart: the chord, the section, the next chord, the log, a planet per layer; along the bottom Filter, Evolve, Motion, Drift; Energy, Wet, Sweep, Volume; the four layer levels; Piece, Tempo, Root, Mode |
| PLAY | Hold, Lock, Play, Forget; Next chord, New chords, Shake all, Reset piece; Keys, Pad base, Sync |
| LAYERS | A column each for Drone, Seq, Lead, Seq 2 and Atmos: Mute, Solo, Nudge, Shake. Space: Nudge, Shake |
| DRONE | Pedal, Reso, Phaser, Attack; Colour, Release, Reverb, Echo; Width, Time, Rate, Intro; its voice |
| SEQ | Density, Octave, Length, Grid; Cutoff, Decay, Reverb, Echo; Repeat A/B and their times; its voice, Time, Rate, Intro |
| LEAD | Phrases, Octave, Glide, Vibrato; Tone, Attack, Reverb, Echo; Width, Time, Rate, Intro; its voice |
| SEQ 2 | Chance, Octave, Length, voice; Cutoff, Decay, Reverb, Echo; Width, Time, Rate, Intro |
| PATTERN | Lane, Accent, Lane chance, Pattern; the lane's 16 steps; their 16 accents (an accent turns its step on) |
| ATMOS | Density, Spread, Drift, Air; Colour, Ring, Reverb, Echo; Width, Time, Rate, Intro; its type and level |
| SPACE | Echo, Echo time, Feedback, Reverb; Blur, Tone, Width, Plate size; Lead in, Bridge, Arc, Memory; Transpose, Tune |
| CHORDS | Chord slot, Chord root ("-" is a gap), kind, type; Play (Endless or Loop), Walk |

The full guide to every control is `docs/USER-GUIDE.md` in the source repository.

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

## Licence and credits

Third Planet is free software under the GNU General Public License, version 3 only (GPL-3.0-only),
because it is built on NACHTFAHRT, which is GPL-3.0-only. See `LICENSE` in the source repository.

- **NACHTFAHRT** by Cranky Man: the concept, the compositions, the engine and the app.
- NACHTFAHRT is a fork of [DUB-1](https://github.com/sbegueria/fm1-dub), itself a fork of
  [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal.
- It draws on [Felucca](https://github.com/hugelton/Felucca) and [Skiff](https://github.com/illuminus5/skiff)
  (Leo Kuroshita, Hügelton Instruments), [fm1_open](https://github.com/m-vave/fm1_open) (M-VAVE; FM voices,
  Pluck after DaisySP, the SEA codec), [JIANT](https://github.com/juanjiant-bit/JIANT), and Jon Dattorro's
  plate reverb.
- The piano samples are from the VCSL Kawai grand (CC0).

`plugins/ThirdPlanet/src/nachtfahrt/LICENSING.md` in the source repository has the full origins table. Third Planet is not affiliated with Cranky
Man, M-VAVE or Hügelton Instruments.
