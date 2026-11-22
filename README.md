# Third Planet

A generative 1970s Berlin School instrument for Akai MPC standalone hardware.

Load it on a track and it starts playing by itself: two drifting sequences, a drone, a gliding Lead with a
memory, an Atmosphere, an echo and a plate reverb, moving through ten pieces that slowly change on their own.
You steer it from the MPC's pads, Q-Links and touch screen.

Third Planet is a port of **NACHTFAHRT** by Cranky Man (<https://github.com/FartyPants/fm1-nachtfahrt>), the
firmware for the M-VAVE FM-1. It runs NACHTFAHRT's own engine and app code, unchanged, so it sounds and behaves
like NACHTFAHRT on an FM-1.

- Target: MPC Live III, MPC OS 3.10.1 or later. Other MPC standalone models running MPC OS 3.10.1+ with
  external plugin support should work, but are not tested.
- Format: VST2 instrument, stereo out, built with the official
  [MPC Plugin SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk).
- Licence: GPL-3.0-only.

## Download

The ready-to-install plugin is in [`release/`](release/): `third-planet-<version>-mpc.zip`. It contains the
plugin folder `Shostak - VST - Third Planet`, an `INSTALL.txt` and a short README.

## Install on the MPC

1. **Turn on external plugins** on the MPC: Preferences → Enhancements (Beta) → turn on both **Enable
   Enhancements** and **External Plugin Support**. Until both are on, the plugin stays hidden.
2. **Copy the plugin.** Unzip the release on a computer. Copy the folder `Shostak - VST - Third Planet` into a
   folder named exactly `VST2 Plugins` at the top level of the MPC's **internal drive** (connect the MPC to the
   computer in Computer mode). Create `VST2 Plugins` if it does not exist. The result should be:

   ```
   <drive>/VST2 Plugins/Shostak - VST - Third Planet/plugin.json
   <drive>/VST2 Plugins/Shostak - VST - Third Planet/VST/...
   ```

   Keep the folder name exactly as it is. On the MPC Live III, plugins on a USB drive may not load (the MPC
   mounts USB drives without permission to run code); the internal drive works.
3. **Restart the MPC** if the plugin does not appear.
4. **Load it.** The plugin is in the plugin browser under **External Plugins**. Create a Plugin program (or
   plugin track) and pick **Third Planet**. It starts playing at once.

To uninstall, delete the `Shostak - VST - Third Planet` folder from `VST2 Plugins`. Projects that used it still
open, without its sound.

Third-party plugins are not supported by inMusic / Akai. A faulty plugin can freeze or crash the MPC: save your
work often.

## Quick start

Set the track's pads so the three pad banks are Third Planet's (Pad Perform on this track):

- Pad Perform on, mode **Chromatic**, root **C**, scale **Chromatic**;
- octave set so bank A pad 1 is **C1, note 36**;
- banks: the **continuous** option, not "Start on root note".

Then:

| Bank | Notes | What a held pad does |
| --- | --- | --- |
| A | 36–51 | **Lead**: plays scale degree 1–16 of the current piece, legato. |
| B | 52–67 | **Effects**: the 16 performance effects (Repeat, Tape stop, Shimmer, Freeze, Supernova, ...). |
| C | 68–75 | **Moves**: Rise, Close, Transpose, Sweep, Open, Shut, Drop, Echo. |

Turn the Q-Links to change the sound; the touch screen has every control, page by page. **Sync** on the PLAY
page makes Third Planet follow the MPC's tempo and transport.

The full guide, with what every knob, switch and pad does: [docs/USER-GUIDE.md](docs/USER-GUIDE.md).

## Harmony Link (optional, off in the release)

The source contains **Harmony Link**: Third Planet can send its harmony (the chord now, the next chord and when
it comes, the form) over USB MIDI to a sequencer on a computer, so that sequencer follows Third Planet. It is
compiled out of the release build. To learn what it is and how to build it in, see
[docs/HARMONY-LINK.md](docs/HARMONY-LINK.md).

## Build from source

See [CONTRIBUTING.md](CONTRIBUTING.md). In short, on macOS or Linux with the SDK's toolchains:

```bash
git clone --recurse-submodules <this repository>
cd third-planet
python3 deps/mpc-plugin-sdk/tools/build.py plugins/ThirdPlanet
```

## Credits and licence

Third Planet is free software under the GNU General Public License, version 3 only ([LICENSE](LICENSE)),
because it is built on NACHTFAHRT, which is GPL-3.0-only.

- **NACHTFAHRT** by Cranky Man: the concept, the compositions, the engine and the app.
- NACHTFAHRT is a fork of [DUB-1](https://github.com/sbegueria/fm1-dub), itself a fork of
  [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal.
- It draws on [Felucca](https://github.com/hugelton/Felucca) and [Skiff](https://github.com/illuminus5/skiff)
  (Leo Kuroshita, Hügelton Instruments), [fm1_open](https://github.com/m-vave/fm1_open) (M-VAVE; FM voices,
  Pluck after DaisySP, the SEA codec), [JIANT](https://github.com/juanjiant-bit/JIANT), and Jon Dattorro's plate
  reverb.
- The piano samples are from the VCSL Kawai grand (CC0).
- The MPC Plugin SDK is by inMusic Brands, under the MIT licence.

[`plugins/ThirdPlanet/src/nachtfahrt/LICENSING.md`](plugins/ThirdPlanet/src/nachtfahrt/LICENSING.md) has the
full origins table, and [`plugins/ThirdPlanet/docs/VENDOR.md`](plugins/ThirdPlanet/docs/VENDOR.md) records the
upstream commit and the small patches. Third Planet is not affiliated with Cranky Man, M-VAVE, Hügelton
Instruments, inMusic or Akai Professional. MPC is a trademark of inMusic Brands.
