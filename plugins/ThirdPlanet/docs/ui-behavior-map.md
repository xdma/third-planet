# NACHTFAHRT control logic: behaviour map for the Third Planet port

This document specifies every control behaviour of the NACHTFAHRT app layer on the M-VAVE FM-1. An
engineer should be able to re-implement the controls on the MPC from it without reading `ui.c`.

It covers what each control does, which engine call it makes, and which state it reads and writes. Drawing,
fonts, the splash screen and the screen reader (`speak`, `sp_*`, `ann_*`) are left out. Section 13 lists the
screen readouts worth mirroring.

**Sources** (read-only reference copies, pinned upstream `f2de0d6`, see `docs/VENDOR.md`):

| Short name | File |
| --- | --- |
| `ui.c` | `src/nachtfahrt/app/ui.c` (3242 lines). A bare line number such as `L1051` means this file. |
| `project.c` | `src/nachtfahrt/app/project.c` |
| `main_fm1.c` | `tests/upstream/main_fm1.c` |
| `app.h`, `plat.h` | `src/nachtfahrt/app/app.h`, `src/nachtfahrt/app/plat.h` |
| `amb_host.c` | `tests/upstream/amb_host.c` (host simulator, script driver) |
| `MANUAL.md` | `tests/upstream/MANUAL.md` |
| `amb.c`, `amb.h` | `src/nachtfahrt/dsp/amb.c`, `src/nachtfahrt/dsp/amb.h` (the engine, unchanged) |

Where the manual or a code comment disagrees with the code, **this document follows the code**. Section 15
lists every disagreement found.

Conventions:
- `proj` is the saved project (`app.h`, `project.c`); `ui` is the transient UI state struct (`L203-244`).
- `knob_set(k, v)` is the UI's single write path for a value. It is not a plain `en_set` (section 3.2).
- Times are in ms from `plat_ms()`.

---

## 1. Engine call model (read this first)

- **Commands.** Every `en_*` command (`en_set`, `en_pool`, `en_play`, `en_fx`, ...) only posts a 32-bit word
  into a 512-entry ring (`amb.c L500-534`). Layout: command in bits 24-31, `a` in bits 16-23 (8 bits),
  `b` in bits 0-15 (signed 16).
  - `en_render()` drains the whole ring at the start of each call (`amb.c L4199`, called at `L6349`).
  - A command therefore takes effect at the next render block, and in posting order.
  - The UI reads back engine state (`en_playing`, `en_frozen`, `en_prog_slot`, ...), which lags its own
    commands by up to one block.
  - `post()` silently drops a command when the ring is full. A piece load posts about 135 words, so several
    loads queued before any render runs can lose commands.
  - The ring is single-producer and has no lock. Call all `en_*` commands from one thread. The FM-1 calls
    them from the main loop, and `en_render` runs in the audio ISR. On the MPC, the simplest port issues the
    commands from the audio thread just before `en_render`.
- **Not queued** (synchronous reads or writes of engine memory):
  - `en_seq_get()` (`amb.c L1512`) reads the live patterns directly.
  - `en_seq()` (`amb.c L1527`) copies the 82 bytes into a pending buffer at once, then posts `C_SEQ`.
  - `en_macro_now()` (`amb.c L535`) compares `en_mac_got` with the UI's private turn counter `mac_sent`.
  - `en_param_info`, `en_param_text`, `en_piece`, `en_piece_name`, `en_walk_chord`, `en_voicing`,
    `en_chord_tones`, `en_chord_name`, `en_chord_label`, `en_drift_bars` and `en_voice_shake_n` are pure
    functions.
- **`en_macro(i, v)`** (`amb.c L530`) posts `C_MACRO` with a 6-bit running count, `mac_sent`, packed into `a`,
  and `v + 50` in `b`.
  - `en_mac_got` (not declared in amb.h) is the count the engine last drained.
  - Until it drains, `en_macro_now()` returns `EN_MAC_BUSY` (-128). This is the handshake `mac_take`
    (section 10.5) depends on.
- **Callbacks the host must supply:** `en_midi_out(st, d1, d2)`, called from inside `en_render`, and
  `en_load` (a global the host writes, section 7.3).

---

## 2. Panel model

### 2.1 Controls (`plat.h L21-31`)

**Buttons, bit index `B_*`.** The order is also `amb_host.c`'s `press` names:

| Bit | Name | Bit | Name |
| --- | --- | --- | --- |
| 0 | FX | 7 | SAVE |
| 1 | SEL | 8 | ARP |
| 2 | ENV | 9 | SEQ |
| 3 | LFO | 10 | PLAY |
| 4 | EDIT | 11 | REC |
| 5 | GLO | 12 | OCT- |
| 6 | HOME | 13 | OCT+ |

**Encoders, `EN_*`:**

| Index | Name | Notes |
| --- | --- | --- |
| 0 | SELECT | |
| 1 | ALGO | Printed ALGORITHM |
| 2 | PRESET | Printed PRESETS |
| 3-6 | K1-K4 | |

`plat_enc(role)` returns the signed detents since the last call (+ = clockwise).

**Keys.** There are 27 keys, F3..G5 (`NKEYS`). Bit k is key k, and key 0 is F3. The pitch class of key k is
`(5 + k) % 12` (F = 5), used by the Chords page (`L942`, `L2656`).

| Key | Note | Kind | Key | Note | Kind | Key | Note | Kind |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | F3 | white 0 | 9 | D4 | white 5 | 18 | B4 | white 10 |
| 1 | F#3 | black 0 | 10 | D#4 | black 4 | 19 | C5 | white 11 |
| 2 | G3 | white 1 | 11 | E4 | white 6 | 20 | C#5 | black 8 |
| 3 | G#3 | black 1 | 12 | F4 | white 7 | 21 | D5 | white 12 |
| 4 | A3 | white 2 | 13 | F#4 | black 5 | 22 | D#5 | black 9 |
| 5 | A#3 | black 2 | 14 | G4 | white 8 | 23 | E5 | white 13 |
| 6 | B3 | white 3 | 15 | G#4 | black 6 | 24 | F5 | white 14 |
| 7 | C4 | white 4 | 16 | A4 | white 9 | 25 | F#5 | black 10 |
| 8 | C#4 | black 3 | 17 | A#4 | black 7 | 26 | G5 | white 15 |

- White key ordinal to key index: `STEP_KEY[16] = {0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26}` (`L973`).
  `FX_KEY` (`L875`) and `amb_host.c`'s `WHITE_K` are the same table.
- Black key ordinal to key index: `BLACK_KEY[11] = {1,3,5,8,10,13,15,17,20,22,25}` (`L977`).
- In this document, "white n" and "black n" are 0-based ordinals. The manual and PERFORM's comments count
  from 1.

**MASTER pot.** It sets the output gain only (section 7.3). The UI never reads it.

### 2.2 Polling and the order of work in one `input()` call (`L1373-1550`)

`input()` runs about every 1 ms. `ui_frame()` (`L3192-3214`) runs every 16 ms or more and calls `input()`
too; otherwise `ui_input_only()` calls it (`main_fm1.c L380-386`). The order inside one call is significant:

1. **Button edges** (`L1377-1405`), in bit order 0..13. For each button that changed:
   - **Press:**
     - clear its bit in `ui.btn_used` and store `ui.down_t[b] = now`;
     - GLO: `ui.reset_done = 0`;
     - PLAY: `button(B_PLAY)` at once (the only action on a press);
     - OCT-/OCT+: `oct_press(b)`.
   - **Release:** the first rule that matches wins.
     1. OCT-/OCT+: `oct_release(b)`.
     2. EDIT: `edit_released(now - down_t[EDIT])`. This ignores `btn_used`.
     3. GLO, not used, held ≥ 500: `do_save()`. If the save left `ui.dirty == 0`, the card "SAVED".
     4. SEL, not used, held ≥ 500: `lock_toggle()`.
     5. Any other button, not used and not PLAY: `button(b)`.
     6. A used button's release does nothing.
2. **Nudge holds** (`L1406-1420`), for ENV, ARP, LFO, SEQ, FX and SAVE (section 9.7).
3. **HOME shake-all hold** (`L1421-1440`).
4. **EDIT long hold**, new chords (`L1441-1448`).
5. **Key edges** (`L1449-1493`), in key order 0..26 (section 5).
6. `ui.act_t = now` if any button or key is held or changed (`L1494`). Autosave uses it.
7. `midi_in()` (`L1496`).
8. **SELECT** (`L1497-1504`), **ALGO** (`L1505-1524`), **PRESET** (`L1525-1528`). Each sets `ui.turn_t = now`
   when turned.
9. **GLO long-hold reset** (`L1529-1540`).
10. **K1-K4** (`L1541-1549`). Each turned knob sets `ui.act_t = ui.turn_t = now` and highlights itself for
    1200 ms (`ui.touched`).

`ui.btn_used` marks a button whose hold, or whose combination with something else, already acted, so its
release is suppressed. It is set by:
- a nudge (the 500 ms hold);
- the HOME shake-all;
- the GLO reset;
- an effects-grid key played while REC is held;
- OCT- and OCT+ down together.

`ui.turn_t` holds the time of the last encoder turn (any encoder). Three hold gestures need "no encoder
turned since the button went down", tested as `(int32_t)(ui.turn_t - ui.down_t[b]) <= 0`:
- the shakes;
- the HOME shake-all;
- the EDIT new chords.

### 2.3 Timing constants

| Constant | ms | Comparison | Where | Meaning |
| --- | --- | --- | --- | --- |
| `GRID_HOLD` | 300 | `>=` | `L876`, `L877`, `L1112` | REC held this long: the grid shows, K1-K4 rest, and REC's release no longer toggles hold |
| `NEXT_HOLD` | 500 | `>=` | `L903`, `L952` | EDIT released after this long: next chord |
| `NUDGE_HOLD` | 500 | `>=` | `L1233`, `L1396`, `L1400`, `L1412` | Nudge (fires while held); GLO release saves; SEL release toggles Lock |
| `SHAKE_HOLD` | 1500 | `>=` | `L1234`, `L1415`, `L1427`, `L1441` | Shake (nudge buttons); HOME shake-all; EDIT new chords. All fire while held, all need no encoder turn |
| `RESET_HOLD` | 1500 | `>` | `L1226`, `L1529` | GLO held: piece back to its recipe (fires while held) |
| Notch window | 700 | `>` | `L605` | Centred knob: the gap that resets the clicks held back at 0 |
| Accel run | 60 | `<` | `L579` | Per-detent gap that keeps an acceleration run going |
| Accel steps | 12 / 25 / 45 | `<` | `L582` | Per-detent gap: 8x / 5x / 3x multiplier |
| Autosave quiet | 4000 | `>=` | `L2705`, `L2709` | No activity this long, resting, and the drone quiet: save |
| Message | 1300 | | `L281` | A header message stays this long |
| Card | 1500 + 300 fade | | `L291-292` | The card in the middle of the screen |
| Knob highlight | 1200 | | `L1547`, `L818`, `L838`, `L1194` | The knob shown as just touched (1500 after a nudge, `L1356`) |
| SELECT ring | 1200 + 250 fade | | `L1136-1137` | The level ring shown |
| HOME Energy rule | 1200 | | `L1180` | Energy shown on the knob rule |
| Update mode | 2000 / 5000 | | `main_fm1.c L341-359` | OCT- + OCT+ held: countdown, then the bootloader. Do not port |

### 2.4 Button behaviour summary

| Button | Tap (released, not used) | Hold (fires while held) | Release after a hold |
| --- | --- | --- | --- |
| PLAY | acts on the **press**: play or rest (7.1) | none | none |
| REC | held < 300: Hold (dub freeze) toggles: `en_freeze(!en_frozen)` (`L1111-1116`) | from the press, white keys are the effects grid (5.4); from 300, the grid shows and the knobs rest | none if held ≥ 300 or a grid key was played |
| EDIT | < 500: open the Chords page (8.4) | ≥ 1500, no turn: `en_prog_rewalk()`, new chords (`L1441-1448`) | ≥ 500: `en_next()`, unless new chords already fired this hold (`L948-955`) |
| SEL | < 500: PERFORM on, or Mix to Perform (6.1) | none | ≥ 500: Lock toggles (`L1400-1401`) |
| GLO | < 500: Setup, or Setup and Arc toggle | > 1500: piece reset (7.5); release suppressed | ≥ 500 and no reset: `do_save()` |
| HOME | < 1500: Nachtfahrt, or Nachtfahrt and Key toggle | ≥ 1500, no turn: shake all (9.8) | suppressed after the shake; **a release between 500 and 1499 is still a tap** (HOME has no nudge) |
| ENV, ARP, LFO, SEQ | < 500: page navigation (4.2) | 500: nudge; 1500, no turn: shake (9.7) | suppressed after the nudge |
| FX | < 500: Space, or Space and Echo toggle | 500: nudge Space; 1500, no turn: shake Space | suppressed |
| SAVE | < 500: Atmosphere pages (4.2) | 500: nudge Atmosphere; 1500, no turn: shake Atmosphere | suppressed |
| OCT- / OCT+ | on release, any duration: octave step on a layer page (4.4) | none | none if the other OCT button was down at the press |

---

## 3. The knob engine

### 3.1 Values a knob can address (`L78-83`)

A knob slot holds either a parameter index (`< P_NPARAMS`, 131) or one of these virtual codes (200 and up):

| Code | `knob_get` (`L390-422`) | Range (`L423-436`) | `knob_set` effect (`L516-565`) |
| --- | --- | --- | --- |
| `P_*` | `proj.par[k]`; for `P_SEQ2OCT`, plus `s2_shift()` | `en_param_info(k)->lo..hi` | 3.2 |
| `K_LEDS` | `proj.leds` | 0..2 | `proj.leds = v`, marks dirty (no engine call) |
| `K_CPU` | CPU % | 0..100 | ignored |
| `K_NONE` | 0 | 0..0 | ignored |
| `K_SLOT` | `ui.slot` | 0..7 | `ui.slot = v` (not dirty, not saved) |
| `K_CQ` / `K_CX` | the chord kind / type of slot `ui.slot`. For a walk slot, the value from `walk_written()` | 0..6 / 0..6 | 8.4 |
| `K_LANE` | `ui.lane` | 0..4 | `ui.lane = v` (not saved) |
| `K_LCH` | `proj.par[P_SCH0 + lane_seq(ui.lane)]` | 0..100 | `knob_set(P_SCH0 + lane_seq(lane), v)` |
| `K_FILTER`, `K_DUB`, `K_ENERGY`, `K_SPACE` | `ui.mac[k - K_FILTER]` | -50..50 | `ui.mac[i] = v; en_macro(i, v)`; not dirty, not saved |
| `K_MODE` | `mode_pos(proj.par[P_MODE])`: the position in `MODE_ORDER` | 0..7 | `set_mode(MODE_ORDER[v])` (3.4) |
| `K_PLAY` | `(P_PMODE & EN_PLOOP) ? 1 : 0` | 0..1 | `knob_set(P_PMODE, (P_PMODE & 3) \| (v ? EN_PLOOP : 0))` |
| `K_SAVE` | `ui.dirty` | none | a turn either way calls `do_save()` (`L593-596`) |

Notes:
- `lane_seq(d) = (d == 1 || d == 3)` (`L86`): lanes 0, 2, 4 belong to the Sequence, lanes 1 and 3 to
  Sequence 2.
- `K_SPACE`, the `EN_MAC_SPACE` macro, sits on no page. The UI never sends `en_macro(EN_MAC_SPACE, ...)`.

### 3.2 `knob_set(k, v)` for a parameter (`L516-533`, `L564`)

1. Return if `k` is `K_CPU` or `K_NONE`.
2. Clamp `v` to the range.
3. If `v == knob_get(k)`, return. The exception is `P_SEQ2OCT` while the shown value includes Motion's shift
   (`s2_shift() != 0`): the write goes ahead even when unchanged, so the hand makes the shown value the home
   value.
4. If `k == P_SEQ2OCT && en_s2oct_shift != 0`, set `ui.s2_hand = 1`. From now on the UI shows the knob without
   the shift, until the engine reports `en_s2oct_shift == 0` (`L3194`). The engine clears the shift itself on
   any `en_set(P_SEQ2OCT)` (`amb.c` `case P_SEQ2OCT`).
5. `proj.par[k] = v; en_set(k, v);`
6. If `k == P_PMODE`, the old value had `EN_PLOOP` and the new one does not, call `en_prog_rotate()` (Loop to
   Endless by hand: the slot playing becomes slot 1).
7. `mark_dirty()` sets `ui.dirty = 1`.

Every parameter write in the UI goes through this path except four. Those write `proj.par` directly and call
`en_set` themselves:
- `set_mode` (`L641`);
- `set_voice` (`L712`);
- `prog_take` (`L925`), which mirrors the engine's slots and makes no engine call;
- `project_apply`.

### 3.3 Turning a knob: `turn(role, k, e)` (`L588-620`)

1. Ignore the turn if `e == 0` or `k` is `K_NONE`/`K_CPU`. If `k == K_SAVE`, call `do_save()` and return.
2. **Centred knob** (`lo < 0 && hi > 0 && hi - lo >= 10`, `L598`). This applies to Tune, the macros, Colour
   (`P_CCOL`), Resonance (`P_CRES`) and the four Noise -50..+50 parameters. The octave knobs (-2..2) are not
   centred knobs.
   - **At 0, a notch** (`L603-614`). The first 2 detents (`NOTCH`) leaving 0 in one direction are absorbed.
     State is kept per encoder role: `held[r]`, `held_t[r]`.
     - The count resets if the last click came more than 700 ms ago, or if the direction reverses.
     - Clicks beyond the absorbed two pass through: the third click leaves 0.
   - Then `n = v + accel(role, e, hi - lo)`.
   - **Stop at zero.** If the turn would cross zero (`v > 0 && n < 0` or `v < 0 && n > 0`), `knob_set(k, 0)`
     is written instead.
3. **Other knobs:** `knob_set(k, knob_get(k) + accel(role, e, hi - lo))`.

**Acceleration `accel(role, s, range)`** (`L570-586`):
- `dt` is the time since this encoder's last call, divided by `|s|` when several detents arrive together.
- `enc_run[role]`, the length of the run, grows by `|s|` while `dt < 60` ms (capped at 255). Otherwise it
  resets to `|s|`.
- No acceleration if any of these holds:
  - `range <= 24`;
  - the host's `spin` command (`accel_off`);
  - `enc_run < 4`, so the first three quick detents are always exact.
- Multiplier: `m = dt < 12 ? 8 : dt < 25 ? 5 : dt < 45 ? 3 : 1`. If `range < 100`, `m` is capped at 3. The
  result is `s * m`.
- Which knobs accelerate:
  - full range (100, up to 8x): 0..100 values, Tempo, Tune, the macros, the -50..50 parameters;
  - capped at 3x: ranges 25..99 (Intro 0..64, Lead in 0..65);
  - never (range 24 or less): everything else.

**Encoders that bypass `turn()`.** SELECT, ALGO and PRESET use the raw detent count: no notch and no
acceleration.

### 3.4 Mode and scale helpers

- **`set_mode(m)`** (`L641-650`):
  ```
  m = wrap(m, 8); proj.par[P_MODE] = m; proj.pool = EN_MODE_POOL[m];
  en_set(P_MODE, m); en_pool(proj.pool); mark dirty
  ```
  The engine's `apply()` has no `P_MODE` case: the mode is only a label. `en_pool` is what changes the
  scale.
- **`MODE_ORDER`** = {Aeolian(5), Ionian(0), Dorian(1), Mixolydian(4), Pent min(7), Pent maj(6),
  Phrygian(2), Lydian(3)} (`L354`). This is the order the Mode knob walks. `mode_pos` returns 0 for an unknown
  mode.
- **`mode_name()`** (`L346`): the mode's name if `proj.pool == EN_MODE_POOL[P_MODE]`, else "Custom".
- **`proj.pool`** is relative to the root (bit i is root + i semitones, bit 0 always set). Changing `P_ROOT`
  does not touch the pool. The engine re-voices the chord (`amb.c` `case P_ROOT`).
- **`toggle_note()`** (`L623-639`, the DUB-1 key-toggles-scale behaviour) is **dead code**: it is never
  called. NACHTFAHRT's UI has no way to make a custom pool. A pool other than a mode can only come from a
  piece recipe or a saved project.

---

## 4. Pages

### 4.1 Views (`L26-34`) and their knobs (`VIEW_KNOB`, `L88-116`; labels `L117-144`)

The "Button path" column gives the presses that reach the view from another page (4.2).

| id | View | Name | Button path | K1 | K2 | K3 | K4 | SELECT | ALGO | OCT- / OCT+ |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `V_DUB` | Nachtfahrt (HOME) | HOME | Filter: `K_FILTER` macro | Evolve `P_EVOLVE` | Motion `P_MOTION` | Drift `P_DRIFT` | Energy macro (9.5) | none | none |
| 18 | `V_KEY` | Key | HOME on Nachtfahrt | Tempo `P_TEMPO` | Root `P_ROOT` | Mode: `K_MODE` (calls `set_mode`) | Transpose `P_TRANS` | none | none | none |
| 1 | `V_CHORD` | Drone | ENV | Pedal `P_PEDAL` | Reso `P_CRES` | Phaser `P_PHASER` | Attack `P_CATT` | level `P_CHORD` | `P_CVOICE` | none |
| 8 | `V_CSND` | Drone sound | ENV x2 | Colour `P_CCOL` | Release `P_DREL` | Reverb `P_DRREV` | Echo `P_DRECHO` | `P_CHORD` | `P_CVOICE` | none |
| 12 | `V_CFM` | Drone move | ENV x3 | Width `P_MW0` | Time `P_MT0` | Rate `P_MR0` | Intro `P_MI0` | `P_CHORD` | `P_CVOICE` | none |
| 2 | `V_TEXTURE` | Sequence | ARP | Density `P_DENSITY` | Octave `P_SEQOCT` | Length `P_SEQLEN` | Grid `P_SEQGRID` | `P_SEQ` | `P_SVOICE` | `P_SEQOCT` ±1 |
| 9 | `V_PSND` | Seq sound | ARP x2 | Cutoff `P_CUTOFF` | Decay `P_DECAY` | Reverb `P_SQREV` | Echo `P_SQECHO` | `P_SEQ` | `P_SVOICE` | `P_SEQOCT` |
| 13 | `V_PFM` | Repeats | ARP x3 | Repeat A `P_TAPA` | A time `P_TAPAT` | Repeat B `P_TAPB` | B time `P_TAPBT` | `P_SEQ` | `P_SVOICE` | `P_SEQOCT` |
| 23 | `V_SMOVE` | Seq move | ARP x4 | Width `P_MW1` (= `P_SWEEP`) | Time `P_MT1` | Rate `P_MR1` | Intro `P_MI1` | `P_SEQ` | `P_SVOICE` | `P_SEQOCT` |
| 3 | `V_BASS` | Lead | LFO | Phrases `P_LDENS` | Octave `P_LOCT` | Glide `P_GLIDE` | Vibrato `P_VIB` | `P_LEAD` | `P_LVOICE` | `P_LOCT` |
| 10 | `V_BSND` | Lead sound | LFO x2 | Tone `P_LTONE` | Attack `P_LATT` | Reverb `P_LDREV` | Echo `P_LDECHO` | `P_LEAD` | `P_LVOICE` | `P_LOCT` |
| 14 | `V_BFM` | Lead move | LFO x3 | Width `P_MW2` | Time `P_MT2` | Rate `P_MR2` | Intro `P_MI2` | `P_LEAD` | `P_LVOICE` | `P_LOCT` |
| 4 | `V_DRUMS` | Sequence 2 | SEQ | Chance `P_SCH1` | Octave `P_SEQ2OCT` | Length `P_SEQ2LEN` | Level `P_SEQ2` | `P_SEQ2` | `P_S2VOICE` | `P_SEQ2OCT` |
| 11 | `V_DSND` | Seq2 sound | SEQ x2 | Cutoff `P_S2CUT` | Decay `P_S2DEC` | Reverb `P_S2REV` | Echo `P_S2ECHO` | `P_SEQ2` | `P_S2VOICE` | `P_SEQ2OCT` |
| 16 | `V_PAT` | Pattern | SEQ x3 | Lane: `K_LANE` | Accent `P_SACCENT` | Chance: `K_LCH` | Play `P_SMODE` | `P_SEQ2` | `P_S2VOICE` | `P_SEQ2OCT` |
| 24 | `V_S2MOVE` | Seq2 move | SEQ x4 | Width `P_MW3` | Time `P_MT3` | Rate `P_MR3` | Intro `P_MI3` | `P_SEQ2` | `P_S2VOICE` | `P_SEQ2OCT` |
| 19 | `V_NSND` | Atmosphere | SAVE | Density `P_NGRAIN` | Width `P_NWIDTH` | Drift `P_NDRIFT` | Air `P_NROOM` | level `P_CRACKLE` | `P_NCHAR` (9.3) | none |
| 21 | `V_NSND2` | Atmos sound | SAVE x2 | Colour `P_NCOL` | Ring `P_NTUNE` | Reverb `P_NPLATE` | Echo `P_NECHO` | `P_CRACKLE` | `P_NCHAR` | none |
| 25 | `V_AMOVE` | Atmos move | SAVE x3 | Width `P_MW4` | Time `P_MT4` | Rate `P_MR4` | Intro `P_MI4` | `P_CRACKLE` | `P_NCHAR` | none |
| 5 | `V_SPACE` | Space | FX | Echo `P_ECHO` | Echo time `P_ETIME` | Feedback `P_FEEDBACK` | Reverb `P_REVERB` | none | none | none |
| 20 | `V_ESND` | Echo | FX x2 | Blur `P_EBLUR` | Tone `P_ETONE` | Width `P_EWIDTH` | Size `P_SIZE` | none | none | none |
| 6 | `V_SETUP` | Setup | GLO | Tune `P_TUNE` | MIDI out `P_MIDI` | Lights: `K_LEDS` | Save: `K_SAVE` | none | none | none |
| 22 | `V_ARC` | Arc | GLO x2 | Lead in `P_LDIN` | Bridge `P_BRIDGE` | Arc `P_ARC` | Memory `P_LMEM` | none | none | none |
| 15 | `V_CHORDS` | Chords | EDIT tap | Slot: `K_SLOT` | Chord: `K_CQ` | Type: `K_CX` | Play: `K_PLAY` | none | walk character (8.4) | none |
| 17 | `V_PERF` | Perform | SEL tap | Filter: `K_FILTER` | Wet: `K_DUB` | Energy: `K_ENERGY` | Sweep `P_SWEEP` | none | none | none |
| 7 | `V_MIX` | Mix (levels) | in PERFORM, hold white 5 (D4) | Drone `P_CHORD` | Seq `P_SEQ` | Lead `P_LEAD` | Seq 2 `P_SEQ2` | none | none | none |

Notes:
- **Names mislead.** The view and parameter names are DUB-1 leftovers: `V_BASS` is the Lead, `V_DRUMS` is
  Sequence 2, `V_TEXTURE` is the Sequence and `V_NSND` is the Atmosphere. Among the parameters, `P_TAPA` is
  "Repeat A", `P_CRACKLE` is the Atmosphere's level, `P_NROOM` is "Air" and `P_NTUNE` is "Ring". Section 14 is
  the full table.
- **SELECT column.** The layer comes from `oct_layer(view)` (`L72-76`): the Drone pages give 0, the Sequence
  pages 1, the Lead pages 2, the Sequence 2 pages (Pattern included) 3, and the Atmosphere pages 4.
  `LAYER_LEVEL = {P_CHORD, P_SEQ, P_LEAD, P_SEQ2, P_CRACKLE}` (`L1139`). On every other view, SELECT does
  nothing; on `V_DUB` it drives Energy.
- **ALGO column.** For the layer pages, ALGO uses `view_voice(page_of(v))` (`L707-711`). On Space, Echo,
  Setup, Arc, Nachtfahrt, Key, Perform and Mix it does nothing. On the Key page the mode is K3 only.
- **Repeated parameters.** Sequence 2's Level `P_SEQ2` appears both as its page's K4 and as its SELECT
  level, and the Mix page's K4 is the same parameter. `P_SWEEP` is the Sequence Move page's Width and also
  PERFORM's K4.

### 4.2 Navigation: `button(b)` (`L1051-1128`)

The rules are checked in order on each tapped release (PLAY on its press):

1. **Leave PERFORM.** If `ui.perf` is set and `b` is one of HOME, ENV, ARP, LFO, SEQ, FX, GLO, SAVE or EDIT
   (`L1054-1061`): `perf_set(0)`, then `set_view(V_DUB)`.
   - If `b == HOME`, stop here: HOME from PERFORM goes to the Nachtfahrt page, never to Key.
   - Otherwise continue with the view now `V_DUB`, so the button opens its page's first view.
   - EDIT never reaches `button()` (its release goes to `edit_released`), so this list's EDIT entry is dead.
     `edit_released` handles PERFORM itself.
2. **The layer's button again, on its first page** (`sound_of(view) >= 0 && b == VIEW_BTN[view]`): go to its
   sound page (`V_CHORD → V_CSND`, `V_TEXTURE → V_PSND`, `V_BASS → V_BSND`, `V_DRUMS → V_DSND`).
3. **The layer's button again, on a sub-page** (`parent_of(view) >= 0 && b == VIEW_BTN[parent]`): go to
   `next_page(view)` (`L47-59`):
   - `V_CSND → V_CFM → V_CHORD`
   - `V_PSND → V_PFM → V_SMOVE → V_TEXTURE`
   - `V_BSND → V_BFM → V_BASS`
   - `V_DSND → V_PAT → V_S2MOVE → V_DRUMS`
4. **FX on Space or Echo:** toggle `V_SPACE ↔ V_ESND`.
5. **GLO on Setup or Arc:** toggle `V_SETUP ↔ V_ARC`.
6. **HOME on Nachtfahrt:** go to `V_KEY`. HOME on Key falls through to rule 7 and returns to `V_DUB`.
7. **A page's button:** the matching view `v < NPAGES`, using `VIEW_BTN` (`L36`): HOME→`V_DUB`, ENV→`V_CHORD`,
   ARP→`V_TEXTURE`, LFO→`V_BASS`, SEQ→`V_DRUMS`, FX→`V_SPACE`, GLO→`V_SETUP`. A layer's button pressed on
   another layer's page always opens that layer's first page.
8. **Switch** on the remaining buttons:
   - **SEL:** if not in PERFORM, `perf_set(1)`; else if the view is `V_MIX`, `set_view(V_PERF)`; otherwise
     nothing. **SEL does not leave PERFORM.**
   - **PLAY:** section 7.1.
   - **REC:** if held ≥ 300 ms, nothing. Otherwise `en_freeze(!en_frozen)` (it reads the engine flag, so a
     double tap faster than one block can misfire).
   - **SAVE:** on an Atmosphere view, cycle `V_NSND → V_NSND2 → V_AMOVE → V_NSND`. Otherwise
     `ui.noise_from = view` and go to `V_NSND`. `noise_from` is never read.
   - **EDIT** and the others: nothing.

`set_view(v)` (`L1045`) sets `ui.view = v` and `ui.touched = -1`, which clears the knob highlight. Changing
view never sends anything to the engine.

The Chords page has no button of its own and no exit rule: any page button leaves it through rule 7, and
SAVE through rule 8. EDIT tapped again on Chords does nothing. `ui.chords_from` is written (`L963`) but never
read.

### 4.3 Per-page behaviour beyond plain parameters

- **Nachtfahrt K1, Perform K1-K3 (macros).** `ui.mac[i] = v; en_macro(i, v)` with -50..50, centred knob with
  a notch. Not saved and not dirty. The engine can move these values by itself, and the UI mirrors them back
  (10.5).
- **Key K3 Mode.** `set_mode(MODE_ORDER[pos])`. It steps by position and clamps at both ends (range 0..7, so
  no acceleration and no wrap). It also replaces the pool.
- **Setup K3 Lights.** `proj.leds`: 0 Off, 1 On (keys plus the button glow), 2 Keys (no glow).
- **Setup K4 Save.** Any turn calls `do_save()`. It shows "Save" while dirty and "Saved" when clean.
- **Pattern page.** K1 picks the lane, `ui.lane` 0..4: Steps (`P_SGATE0`), Steps 2 (`P_SGATE1`), Octaves
  (`P_SOCT0`), Octaves 2 (`P_SOCT1`), Skips (`P_SSKIP0`); the lane's parameter is `P_SGATE0 + lane`.
  - K2 Accent `P_SACCENT` is the accent strength.
  - K3 is the lane's chance: `P_SCH0` for lanes 0, 2 and 4, `P_SCH1` for lanes 1 and 3. `P_SCH1` is the same
    parameter as the Sequence 2 page's K1.
  - K4 Play, `P_SMODE`: 0 Auto, 1 Pattern.
  - The keys write steps (5.5).
- **Chords page.** Section 8.4.
- **Sequence 2's Octave** (`P_SEQ2OCT`, Sequence 2 page K2, and OCT on its pages). It shows home plus
  `en_s2oct_shift`, Motion's temporary ±1 shift. Section 3.2 steps 3-4 cover the write.
- **Brightness and Density "dots".** These are display only (13.2).

### 4.4 OCT- / OCT+ (`L1130-1207`)

- **Press** (`oct_press`): if the other OCT button is already down, mark both used, so neither steps.
- **Release** (`oct_release`): if not used, `oct_step(b)`.
- **`oct_step(b)`:**
  - `l = oct_layer(view)`. Act only on the Sequence pages (`P_SEQOCT`, -2..2), the Lead pages (`P_LOCT`,
    -2..1) and the Sequence 2 pages (`P_SEQ2OCT`, -2..1), per `LAYER_OCT` (`L1140`).
  - On the Drone pages, the Atmosphere pages and pages of no layer, OCT does nothing.
  - The write is `knob_set(k, knob_get(k) ± 1)`, clamped. For Sequence 2, `knob_get` includes the shift, so
    the step goes from the shown value (`L1189`).
  - If the page's K2 is that parameter, K2 is highlighted for 1200 ms.
- **OCT does not transpose the white keys** on any page except the Lead pages, where it changes `P_LOCT`.
  That moves the Lead, and so the keys' notes.
- **LEDs** (`L2691-2697`): on a layer page with an octave, OCT+ is lit while the value is above
  `ui.oct_home[l]` and OCT- while it is below. `oct_home` is captured by `oct_home_take()` (`L1143`) at boot,
  on a piece change and on a reset.

---

## 5. Keys (`L1449-1493`)

### 5.1 Routing priority for one key edge (key `i`, `down` = pressed)

1. **Black key released:** always `black_set(black_of(i), 0)` (`L1454-1455`), whatever the mode.
2. **White key released:** always `en_lead_key(st, 0)`, where `st` is its white ordinal (`L1456-1464`). The
   engine ignores a release that is not its current key. Then continue.
3. If (`REC held && down`) or (`!down && ui.fx != 0`): `fx_key(i, down)`. If the key is a grid key (any white
   key), stop here (`L1465-1467`).
4. If REC is held: stop. Black key presses do nothing while REC is held (`L1468-1469`).
5. **Pattern page and not in PERFORM:** on a press, `pattern_key(i, keys)`. Stop (`L1470-1474`).
6. **Chords page and not in PERFORM:** on a press, `slot_key(i)` for any key, white or black. Stop
   (`L1475-1479`).
7. **In PERFORM:** `perf_key(i, down)` (`L1480-1481`).
8. **Normal playing** (`L1482-1491`): a white key press calls `en_lead_key(st, 1)`; a black key press calls
   `black_set(b, 1)` and shows `BLACK_NAME[b]`.

Only REC changes what the keys do while a button is held. PERFORM, Pattern and Chords are modes (views), not
holds. Other holds (EDIT, nudge buttons and so on) do not change key behaviour. On the Pattern page, a held
black key changes what a white key does (5.5).

### 5.2 Normal playing: white keys play the Lead

- **Press:** `en_lead_key(st, 1)` with `st` = white ordinal 0..15. **The scale degree is `st`**, counted from
  the root (`L1486-1487`).
- **Release:** `en_lead_key(st, 0)`, always sent (5.1 step 2).
- **Engine mapping** (`amb.c L2059-2067` `ld_deg_raw`, `L3138` `lead_press(id = st, d = st)`):
  ```
  ns = scale size (pool count; 7 if empty)
  o  = floor(d / ns)
  k  = d mod ns
  note = 60 + (P_ROOT % 12) + 12*P_LOCT + 12*o + steps[k]     // steps = the pool's semitones, ascending
  note = clamp(note, 36, 100)
  ```
  So white 0 (F3) is the root at MIDI `60 + root + 12*LeadOct`. The physical pitch of the key does not
  matter, and nothing on the UI side shifts the keys. The Lead is monophonic: the last key held sounds, and a
  press while another key is held plays legato.
- **LED:** the white key `STEP_KEY[en_lead_deg]` is lit while the Lead sounds degree 0..15 (`L2680-2685`,
  only when `proj.leds != 0`).

### 5.3 Normal playing: black keys play 11 effects (`L974-999`)

| Black | Key | Effect | Engine bit |
| --- | --- | --- | --- |
| 0 | F#3 | Take-off | `EN_FX_TAKEOFF` (9) |
| 1 | G#3 | Supernova | `EN_FX_SUPERNOVA` (15) |
| 2 | A#3 | Twitter | `EN_FX_TWITTER` (14) |
| 3 | C#4 | Repeat | `EN_FX_REPEAT` (0) |
| 4 | D#4 | Shimmer | `EN_FX_SHIMMER` (3) |
| 5 | F#4 | Freeze (the cloud; not REC's Hold) | `EN_FX_FREEZE` (10) |
| 6 | G#4 | Octave | `EN_FX_OCTAVE` (12) |
| 7 | A#4 | Return | `EN_FX_RETURN` (13) |
| 8 | C#5 | Rise | move `EN_MV_RISE` (1) |
| 9 | D#5 | Close | move `EN_MV_CLOSE` (2) |
| 10 | F#5 | Transpose | move `EN_MV_TRANSPOSE` (4) |

**`black_set(b, on)`** (`L990-999`):
```
if BLACK_DO[b] >= 100:  ui.mv  = on ? ui.mv | move : ui.mv & ~move
else:                   ui.bfx = on ? ui.bfx | (1 << fx) : ui.bfx & ~(1 << fx)
en_fx(ui.fx | ui.bfx);      // both are always sent, in this order
en_move(ui.mv);
```
- **Press** in normal playing: `black_set(b, 1)`. Only in normal playing (5.1 step 8).
- **Release**, in any mode: `black_set(b, 0)`. A black key pressed on the Pattern page, the Chords page, in
  PERFORM or with REC held therefore still sends `en_fx`/`en_move` with its bit cleared on release, which is
  harmless.
- A grid white key and a black key for the same effect OR together in `en_fx`.
- **LED:** each black key is lit unless its own effect is held (`ui.bfx` or `ui.mv`), when `proj.leds != 0`
  (`L2686-2689`). An effect held from the grid does not darken the black key.

### 5.4 The effects grid (REC held) (`L871-894`)

- REC held, from the press with no delay: the 16 white keys are the 16 `EN_FX_*` effects in enum order,
  white `e` → effect `e` (`FX_KEY = STEP_KEY`):
  - Repeat, Time, Tape stop, Shimmer;
  - Low-pass, High-pass, Sweep, Phaser;
  - Reverb, Take-off, Freeze, Wash;
  - Octave, Return, Twitter, Supernova.
- **`fx_key(i, down)`:**
  - **Press:** `ui.fx |= 1 << e`; `ui.btn_used |= REC`, so REC's release will not toggle Hold; show the
    name.
  - **Release:** `ui.fx &= ~(1 << e)`.
  - Then `en_fx(ui.fx | ui.bfx)`.
- **Latching.** An effect stays on while its key is held, even after REC is released: releases route to
  `fx_key` whenever `ui.fx != 0`.
- **The grid display** (`grid_up()`, `L877`) shows when `ui.fx != 0` or REC has been held ≥ 300 ms. While it
  is up, K1-K4 turns are ignored (`L1543-1544`). SELECT, ALGO and PRESET still work.
- **LEDs** (`L2659-2662`): the first key of each group of four (white 0, 4, 8, 12) and each held effect are
  lit.
- While REC is held, white key presses never reach the Lead, PERFORM, Pattern or Chords. Black key presses are
  ignored.
- `L1468-1469` stops **every** key edge while REC is held, releases included, even with `ui.fx == 0`. A key
  pressed before REC and released while REC is down never reaches `perf_key` (quirk Q7).

### 5.5 Pattern page keys (`L968-1042`)

`pattern_key(i, keys)`, on a press only:

1. `st` = white ordinal of `i`. A black key returns at once: black keys only modify white presses.
2. `black` = the held keys mask with all white keys removed. It is non-zero if any black key is held.
3. **First edit** (`P_SMODE == 0`): take over what Auto plays:
   ```
   for k in 0..4: knob_set(P_SGATE0 + k, (int16) en_seq_lane[k])
   knob_set(P_SACC0, (int16) en_seq_acc[0]); knob_set(P_SACC1, (int16) en_seq_acc[1]); knob_set(P_SMODE, 1)
   ```
4. `d = ui.lane`; `m = (uint16) P_SGATE0+d`; `a = P_SACC0 + lane_seq(d)`. Lanes 0, 2 and 4 use the Sequence's
   accents `P_SACC0`; lanes 1 and 3 use Sequence 2's `P_SACC1`.
5. **With a black key held:** `knob_set(a, acc ^ (1 << st))` and `knob_set(P_SGATE0+d, m | 1 << st)`. This
   toggles the accent and forces the step on.
6. **Otherwise:** `knob_set(P_SGATE0+d, m ^ (1 << st))`, which toggles the step.

Values are stored as int16 (bit i = step i), so a step-15 bit makes the value negative. Every write goes
through `knob_set`, so each is an `en_set` and marks the project dirty.

**LEDs** (`L2645-2649`): the lane's 16 steps on the white keys. The source is `P_SGATE0+lane` in Pattern
mode, or the live `en_seq_lane[lane]` in Auto mode (`lane_steps`, `L1000`).

### 5.6 Chords page keys

`slot_key(i)` (`L938-946`) takes any of the 27 keys. Its intent is to write key `i`'s pitch class
`(5 + i) % 12` as the slot's root.
```
slot_write(ui.slot);                         // a walk slot: converted to a written chord first (8.4)
knob_set(P_CR0 + slot, (5 + i) % 12);
if (P_CQ0 + slot == 0) knob_set(K_CQ, EN_CQ_MINOR);   // a gap becomes a minor chord
```
On a gap slot this misbehaves (section 15, quirk Q1).

**LEDs:** every key whose pitch class equals the slot's root. For a walk slot this is the root
`walk_written` gives; a gap lights nothing (`L2650-2658`).

### 5.7 PERFORM keys (`L749-850`)

`PERF_KEY` (`L760-762`) and the manual count from 1; here the ordinals are 0-based:

| Keys | Role | Press | Release |
| --- | --- | --- | --- |
| white 0-4 = keys 0, 2, 4, 6, 7 (F3 G3 A3 B3 C4) | mute Drone, Seq, Lead, Seq2, Atmos | toggle the mute (below) | none |
| white 5 = key 9 (D4), `PERF_SHIFT` | Mix (levels on the knobs) while held | `set_view(V_MIX)` | `set_view(V_PERF)` |
| white 6-10 = keys 11, 12, 14, 16, 18 (E4 F4 G4 A4 B4) | moves Sweep, Open, Shut, Drop, Echo | `ui.moves \|= 1 << n` | `ui.moves &= ~(1 << n)` |
| black 0-4 = keys 1, 3, 5, 8, 10 (F#3 G#3 A#3 C#4 D#4) | solo Drone, Seq, Lead, Seq2, Atmos | `ui.solo ^= LAYER_MUTE[n]` | none |
| white 15 = key 26 (G5), `PERF_FORGET` | Forget | `en_lead_forget()` | none |
| every other key | none | none | none |

- A move key press or release sends `en_perf(ui.moves)`. Bits: `EN_PF_SWEEP` 1, `EN_PF_OPEN` 2,
  `EN_PF_CLOSE` 4, `EN_PF_DROP` 8, `EN_PF_ECHO` 16.
- **The mute mask** (`L766-771`):
  ```
  LAYER_MUTE = { EN_M_CHORD(1), EN_M_PINGS(2), EN_M_BASS(4), EN_M_KICK|EN_M_HATS(8|16), EN_M_NOISE(32) }
                 Drone          Sequence       Lead         Sequence 2 (two bits)       Atmosphere
  ALL_MUTE   = 63
  mute press:  if ((ui.mutes & m) == m) ui.mutes &= ~m; else ui.mutes |= m;
  en_mute( ui.mutes | (ui.solo ? (ALL_MUTE & ~ui.solo) : 0) )      // perf_mutes(), sent after every mute or solo change
  ```
  - With any solo on, every non-soloed layer is muted on top of the explicit mutes.
  - A layer that is both soloed and muted stays muted.
  - "Heard" (`perf_sounds`, `L772`) means not soloed out and not fully muted.
- On the Mix view, a mute or solo press also highlights that layer's Mix knob for 1200 ms (`ui.touched =
  min(layer, 3)`). Sequence 2 and the Atmosphere both highlight K4.
- Black keys 5-10 do nothing in PERFORM, since solos replace the effects. Their releases still clear the
  black-key effect bits (5.1 step 1).
- **LEDs** (`L2663-2678`):
  - SEL is lit.
  - White 5 blinks (450 ms half period) unless the view is Mix.
  - Mute keys are lit while the layer is heard.
  - Move keys are lit unless held.
  - Solo keys are lit when not soloed and blink (250 ms half period) when soloed.
  - The Forget key is lit unless held.

---

## 6. PERFORM, Lock, Hold and the other performance state

### 6.1 Entering and leaving PERFORM

- **`perf_set(1)`** (`L853-869`), from a SEL tap: `ui.perf = 1`; remember `perf_from` if the view is not
  Mix or Perform; `set_view(V_PERF)`.
- **`perf_set(0)`:**
  1. If any move is held: `ui.moves = 0; en_perf(0)`.
  2. If the view is Mix or Perform, `set_view(perf_from)`. `button()` immediately overrides this with
     `V_DUB` (4.2 rule 1), so `perf_from` has no visible effect.
- **Mutes and solos survive leaving PERFORM** and a piece change. They are never saved and never cleared
  except by toggling the keys again.
- **PERFORM is left by** a page button (HOME, ENV, ARP, LFO, SEQ, FX, GLO, SAVE) or an EDIT tap
  (`edit_released`, `L959-962`). SEL never leaves it.
- **Inside PERFORM:**
  - K1-K4 are the macros and Sweep (Perform view) or the levels (Mix view);
  - PRESET still changes piece;
  - SELECT and ALGO do nothing;
  - REC's grid still works and takes priority over the keys;
  - nudge buttons still nudge (and do not change view, `L1348`);
  - EDIT's hold actions still work.

### 6.2 Lock (`L781-788`)

- A SEL release after holding ≥ 500 ms calls `lock_toggle()`: `ui.lock = !ui.lock; en_lock(ui.lock)`.
- It is not saved; it is off at boot (`ui_init` zeroes `ui`).
- Lock also suppresses Evolve's automatic nudges (10.3).

### 6.3 Hold, the dub freeze (REC tap)

- `en_freeze(!en_frozen)` (`L1114-1115`). The engine flag `en_frozen` is the truth, read back for the LED and
  the toggle.
- It is not saved. A piece change does not clear it.
- It is a different effect from the grid and black-key "Freeze" (`EN_FX_FREEZE`, the cloud).

### 6.4 Forget (PERFORM white 15)

`en_lead_forget()` (`L798-805`). This only acts on the engine's Lead memory. No parameter changes and nothing
is saved.

### 6.5 Moves and effects summary

| Mask | Composed from | Sent by | Saved |
| --- | --- | --- | --- |
| `en_mute` | `ui.mutes \| (solo ? ALL & ~solo : 0)` | `perf_mutes()` | no |
| `en_perf` | `ui.moves` (PERFORM white 6-10 held) | `perf_key`, `perf_set(0)` | no |
| `en_fx` | `ui.fx` (grid) `\| ui.bfx` (black 0-7) | `fx_key`, `black_set` | no |
| `en_move` | `ui.mv` (black 8-10) | `black_set` | no |
| `en_lock` | `ui.lock` | `lock_toggle` | no |
| `en_macro(i, v)` | `ui.mac[i]` | `knob_set`, mirrored by `mac_take` | no |

---

## 7. Transport, clock, MIDI, audio, pieces and saving

### 7.1 PLAY (on the press) (`L1099-1110`)

```
if (en_playing) { en_play(0); }                       // rest: the tails ring on
else            { en_seed(plat_ms()); en_play(1); }   // play: fresh entropy, then start from the top
```
- `en_play(1)` posts `C_PLAY`. The engine calls `play_start(1)` (`amb.c L4123`) only if it is not playing.
  This resets the grid, the arc (`ar_start`, so Emerge and the Intros run again), the sequences' positions and
  the Lead's breath. It sends MIDI Start (0xFA) unless on an external clock.
- `en_play(0)` calls `play_stop()` (`amb.c L4157`), which sends MIDI Stop (0xFC) unless on an external clock.
- The toggle reads `en_playing`, so it follows a MIDI clock that started or stopped the engine.
- **At boot**, `ui_init()` calls `en_play(1)` if the engine is not playing (`L3186-3187`). The instrument
  plays from power-on.

### 7.2 MIDI in (`L1209-1223`), polled once per `input()` call

All channels are accepted. Running status is not handled: the input is 4-byte USB-MIDI packets.

| Message | Call |
| --- | --- |
| 0xF8 clock | `en_clock(EN_CLK_TICK)` |
| 0xFA start | `en_clock(EN_CLK_START)` |
| 0xFB continue | `en_clock(EN_CLK_CONTINUE)` |
| 0xFC stop | `en_clock(EN_CLK_STOP)` |
| 0x9n note, velocity > 0 | `en_lead_midi(note, 1)` |
| 0x8n note, or 0x9n with velocity 0 | `en_lead_midi(note, 0)` |
| anything else (CC, bend, aftertouch, SysEx) | ignored; velocity is ignored too |

**Engine side of the clock** (`amb.c L4230-4244`):
- Any clock message sets `en_ext = 1` and `ext_age = 0`.
- TICK, while playing, advances 1/24 of a beat (6 per sixteenth).
- START calls `play_start(1)` even if already playing.
- CONTINUE calls `play_start(0)` if resting.
- STOP calls `play_stop` if playing.
- `en_ext` drops back to 0 after 100 `en_render` calls with no clock message (`amb.c L6350`). That is about
  0.58 s at 256 frames, but it counts render calls, not time (section 15, quirk Q8).
- While `en_ext` is set, the engine sends no clock, start or stop out.

**Engine side of notes** (`amb.c L4252-4270`):
- The note maps to the nearest scale tone, the lower one on a tie:
  `rt = 60 + root`, `rel = note - rt`, `oc = floor(rel / 12)`, `pc = rel - 12*oc`. Search the scale tones `t`
  in octaves -1, 0 and +1 for the nearest to `pc`.
- The degree is `(oc + s)*ns + k`. It plays through the same `lead_press` as the keys, with `id = note`, so it
  shifts with `P_LOCT` too.
- Monophonic: the last note held sounds.

**MIDI out** comes from the engine through `en_midi_out` (Drone on channel 1, the Sequence on
channel 2, the Lead on channel 3, Sequence 2 on channel 4, plus clock). `P_MIDI = 0` sends All Notes Off on channels 1-4 and 10, and gates the output.
On the FM-1 the app forwards each event to USB as a packet (`amb_host.c` `en_midi_out`).

### 7.3 MASTER and the audio block (`main_fm1.c`)

**Gain** (`main_fm1.c L323-331`). The ADC reading `a` is 0..1023 (10-bit, inferred from the `512*16` start value and the square law). `knob` starts at `512*16`. In each main
loop pass:
```
knob += (a*16 - knob) / 8;                  // one-pole smoothing, integer
k10   = knob / 16;
master_q12 = (k10 * k10) >> 8;              // square law, 0 .. ~4088
```
The engine applies `gain = master_q12 / 4096 * 2` (`amb.c L6343`). Full MASTER is x2 (+6 dB) and the middle of
the pot is about x0.5. The host simulator passes `master` (0..4096, default 2048 = x1) straight through. On
the MPC, use 2048 for unity or map a level control the same way.

**Per audio block** (`main_fm1.c L28-64`):
- `en_render(out, 256, master_q12)` with 256 frames at 44.1 kHz.
- Then `en_load = min(255, block_time_us * 100 / budget_us)`. The engine reads `en_load` at the start of the
  next render and sheds load (quiet tails, cloud grains) for 0.5 s when it is over 75.
- The FM-1 has no other per-block work. There is no MIDI here: MIDI in is drained in the UI loop.
- On the MPC, set `en_load` from the plugin's own CPU measure, or leave it at 0 to never shed.

**Update session** (OTA, `main_fm1.c L367-374`): `en_play(0); en_panic();`. This is the app's only
`en_panic` call: there is no PANIC control.

### 7.4 Pieces: PRESET (`L1525-1528`, `set_piece` `L736-747`)

A PRESET turn of `e` detents calls `set_piece(proj.piece + e)` with the raw count, wrapping modulo 10
(`project_piece`). It works on every view, including PERFORM.

`set_piece(i)`, in order:

1. `mac_home()`: `ui.mac[0..3] = 0` (UI copy only; no `en_macro` is sent).
2. `project_piece(i)` (`project.c L125-130`):
   - **`keep()`** (`L32-41`) stores the current piece into its slot: `slot.par[] = proj.par[]` (all 131,
     globals included), `slot.pool`, `slot.screen`, `en_seq_get(slot.pings)` (synchronous read of the live
     patterns), `slot.pings_ok = 1`.
   - `proj.piece = wrap(i)`.
   - **`take(&slot[i])`** (`L22-30`): `proj.par[p] = slot.par[p]` for every p **except `P_TUNE` and
     `P_MIDI`**, which stay global; `proj.pool = slot.pool`; `proj.screen = slot.screen % 3`.
3. `project_apply()` (`project.c L141-149`):
   ```
   for i in 0..P_NPARAMS-1: en_set(i, proj.par[i])     // all 131, in index order, P_TUNE and P_MIDI included
   en_pool(proj.pool)
   en_seq(slot.pings_ok ? slot.pings : NULL)           // the piece's kept patterns, or none (grown afresh)
   ```
4. `oct_home_take()` captures the octave LED homes.
5. `en_seed(plat_ms())`.
6. `en_regrow()`. The engine's `C_REGROW` (`amb.c L4277-4292`) does all of the following:
   - resets all four macros to 0 (`macro(a, 0)`) and the hand's hold on them;
   - regrows the patterns and loops;
   - re-applies the kept patterns if `en_seq` gave some;
   - restarts the arc;
   - reseeds the Lead's ideas and seeds the written theme if `theme_pc == 9`;
   - lets the Lead's note go.
7. `mark_dirty()`.

Not touched by a piece change: mutes, solos, moves, effects, Lock, Hold, the transport state and the view.
`en_piece_theme()` is **never called** by the app (section 15, quirk Q2), so Umeda's written theme never
arms on the FM-1 build at this pin.

### 7.5 Recipes and "held GLO on: back to recipe"

- **A recipe** (`project.c L13-18`) is `en_piece(i, slot.par, &slot.pool)`, with `pings_ok = 0` and
  `screen = 0`.
- **GLO held > 1500 ms** (`L1529-1540`), once per hold (`ui.reset_done`): `ui.btn_used |= GLO`, so the
  release does not save. Then, in order:
  1. `project_piece_reset()` (`project.c L132-139`): a recipe into a temporary slot, keeping the current
     `screen`; `take()` it, so globals stay; `proj.slot[piece].pings_ok = 0`. The stored slot's `par` is not
     overwritten until the next `keep()`.
  2. `project_apply()`: the `en_set` loop, `en_pool`, and `en_seq(NULL)` because `pings_ok = 0`.
  3. `oct_home_take()`.
  4. `mac_home()`.
  5. `en_regrow()`. There is **no `en_seed`** here, unlike `set_piece`.
  6. `mark_dirty()`. The reset is not saved until the next save.

### 7.6 Boot (`main_fm1.c L289-314`, `amb_host.c` `boot()`)

```
en_init();          // engine defaults, fixed seeds, regrow() inside
project_load();     // the saved project, validated; else project_defaults()
project_apply();    // en_set x131, en_pool, en_seq(pings or NULL)
ui_init();          // ui zeroed, oct_home_take(), en_play(1) if not playing
```
There is **no `en_seed` and no `en_regrow`** at boot. The arc starts from `en_play(1)` → `play_start(1)`.

- **`project_load`** (`project.c L101-117`):
  1. Read the object.
  2. Require the exact size, `magic == 0x314C5242` ("BRL1") and `format == 10`.
  3. Clamp every parameter to its current range (`pars_clamp`).
  4. Validate (`project_valid`, `L88-99`): every pool has the root bit, no bits above 11 and at least 3
     notes; every parameter is in range; `piece < 10`.
  5. On any failure, `project_defaults()`.
- **`project_defaults`** (`L43-55`):
  ```
  proj zeroed
  slot[i] = recipe(i) for all 10
  proj.par[] = en_param_info defaults
  take(slot[0])       // Timewind, with Tune 0 and MIDI out 1 from the defaults
  proj.leds = 1
  ```

### 7.7 Saving

- **`do_save()`** (`L507-515`): `project_save()`, which is `keep()` then write the whole `project_t`. On
  success `ui.dirty = 0` and "SAVED"; otherwise "NOT SAVED / FLASH ERROR".
  - Called from a GLO release after ≥ 500 ms (unless the reset fired), and from any turn of Setup's K4.
  - A GLO-release save also shows a card if it succeeded.
- **Autosave** (`L2703-2715`), once per frame. It saves when all of these hold:
  - `ui.dirty`;
  - `!en_playing`;
  - no button and no key held;
  - `now - ui.act_t >= 4000`;
  - `en_pad_level <= 0.01`.
  
  `ui.act_t` is refreshed by held or changed buttons and keys and by K1-K4 turns, but not by SELECT, ALGO or
  PRESET turns. The quiet-and-resting rule exists because a flash write stalls the FM-1's audio. The MPC
  needs no autosave: the host saves plugin state.
- **`mark_dirty()`** is called by:
  - every `knob_set` of a parameter or `K_LEDS`;
  - `set_mode`, `set_voice` and `set_piece`;
  - the reset;
  - the nudges, Evolve's included (through `knob_set`).

  It is not called by `prog_take` (the engine's own chord walk), the macros, mutes, solos, the lane or slot
  selection, Lock, Hold or the effects.

---

## 8. Chords (EDIT)

### 8.1 EDIT gestures (`L897-966`, `L1441-1448`)

- **Held ≥ 1500 ms with no encoder turned since the press** (fires while held, once per hold, through the
  file-static `edit_new`):
  - `en_prog_rewalk()`;
  - the card "NEW CHORDS: walk name", where the name is `EN_WALK_NAME[(P_PMODE & 3) % 3]`;
  - the release then does nothing, and clears `edit_new`.
- **Released after ≥ 500 ms** (and no new chords this hold): `en_next()`.
  - The engine changes chord on the next bar while playing, or at once while resting (`amb.c L4218`).
  - EDIT held past 1500 ms with a turn in between also ends here.
- **Released after < 500 ms:**
  - already on Chords: nothing;
  - otherwise, if in PERFORM: `perf_set(0); set_view(V_DUB)`;
  - then `chords_from = view` and `set_view(V_CHORDS)`.
- EDIT's hold does not set `btn_used`, and EDIT never reaches `button()`.

### 8.2 The slot model (amb.h `L73-78`)

There are 8 slots, `P_CR0..7` (root), `P_CQ0..7` (kind), `P_CX0..7` (type). A slot is one of three things:

| Slot | Condition | Fields |
| --- | --- | --- |
| Written | `CQ != 0` | CR = absolute pitch class 0..11, CQ = `EN_CQ_*` kind, CX = `EN_CX_*` type |
| Walk | `CQ == 0 && CR != 0` (`slot_is_walk`, `L364`) | CR = scale degree + 1 (1..7), CX & 1 = voicing shape |
| Gap | `CQ == 0 && CR == 0` (`slot_is_gap`, `L365`) | the chord before holds |

- **`P_PMODE`:** bits 0-1 are the walk character (`EN_WALK_HOME` 0, `WANDER` 1, `FREE` 2); bit 2 is
  `EN_PLOOP`, Loop.
- **Endless** (Loop off): slot 1 plays, the slots move up at each change, and the walk writes slot 8.
- **Loop:** the slots play in turn, up to the last non-gap slot (`prog_count`, `L904`).
- **`walk_written(i, &r, &q, &x)`** (`L373-376`): `en_walk_chord(pool, P_ROOT, (CR - 1) % scale_steps(), ...)`.
  This is the chord a walk slot would be if written down.
- **`slot_write(i)`** (`L379-388`): for a walk slot only, `knob_set(CR, r)`, `knob_set(CX, x)`,
  `knob_set(CQ, q)`, in that order. It turns the walk chord into a written chord that sounds the same.

### 8.3 The engine owns the slots: `prog_take()` (`L925-936`), every frame

```
if en_prog_gen != ui.prog_gen:
    ui.prog_gen = en_prog_gen
    for i in 0..7: proj.par[P_CR0+i], [P_CQ0+i], [P_CX0+i] = en_prog_slot[i][0], [1], [2]
```
- No `en_set` and no dirty mark. The engine rewrites the slots as the walk moves or Endless shifts.
- The UI's copy is a mirror, and it is saved with the project.
- A UI edit is posted to the engine first. The engine applies it and publishes a new generation, which
  `prog_take` brings back.

### 8.4 Chords page controls

- **K1 Slot** (`K_SLOT`): `ui.slot` 0..7. View state only.
- **K2 Chord** (`K_CQ`, `L539-546`):
  1. `slot_write(slot)`.
  2. `knob_set(P_CQ0+slot, v)`.
  3. If `v == 0` (Empty): also `knob_set(P_CR0+slot, 0)` and `knob_set(P_CX0+slot, 0)`, making a gap.
  
  On a walk slot the starting value is the walk chord's kind.
- **K3 Type** (`K_CX`): `slot_write(slot)`, then `knob_set(P_CX0+slot, v)`. On a gap this writes CX only, and
  the slot stays a gap.
- **K4 Play** (`K_PLAY`): `knob_set(P_PMODE, (P_PMODE & 3) | (v ? 4 : 0))`. Going from Loop to Endless also
  calls `en_prog_rotate()` (3.2 step 6). The engine's `apply(P_PMODE)` (`amb.c L3864-3875`) handles both
  directions.
- **ALGO** (`L1511-1519`):
  ```
  w = ((P_PMODE & 3) + e % 3) mod 3          // raw detents, wraps
  knob_set(P_PMODE, (P_PMODE & EN_PLOOP) | w)
  en_prog_rewalk()                           // always, even if w did not change
  ```
- **Keys:** section 5.6.

`en_voicing`, `en_chord_tones` and `en_chord_name` are used only to label walk slots (`slot_label`, `L912`).
They are display only.

---

## 9. Other controls in detail

### 9.1 SELECT

- **On a layer's pages**, `level_turn(l, e)` (`L1165-1172`): `knob_set(LAYER_LEVEL[l], par + e)`, raw, 1 per
  detent. It also shows the level ring.
- **On Nachtfahrt**, `energy_turn(e)` (`L1173-1181`): `knob_set(K_ENERGY, ui.mac[2] + e)`, which sends
  `en_macro(EN_MAC_ENERGY, v)`. This path bypasses `turn()`, so there is no notch at 0.
- **Elsewhere:** nothing, but `ui.turn_t` is still updated, which blocks a pending shake.

### 9.2 ALGO on a layer page: `set_voice(p, proj.par[p] + e)` (`L712-726`)

```
n = hi - lo + 1; v = wrap(v, lo..hi); proj.par[p] = v; en_set(p, v); mark_dirty
```
It wraps (after the last voice, the first), with raw detents and no acceleration.

| Layer | Parameter | Voices | Shake picks from |
| --- | --- | --- | --- |
| Drone | `P_CVOICE` | 0..5: Organ, Strings, Horn, Saws, Glass, Choir | all 6 |
| Sequence | `P_SVOICE` | 0..16: 2600, Moog, Synthi, Pulse, Glass, Pluck, then 10 FM voices, then Piano | 0..5 |
| Lead | `P_LVOICE` | 0..9: Odyssey, Mini, Flute, Theremin, Synthi, FM Lead, FM Reed, FM Brass, Choir, Piano | 0..4 |
| Sequence 2 | `P_S2VOICE` | 0..17: 2600, Moog, Synthi, Pulse, Glass, Sub, Pluck, then 10 FM voices, then Piano | 0..6 |

The "Shake picks from" column is `en_voice_shake_n`.

### 9.3 ALGO on the Atmosphere pages (`L1507-1510`)

`c = wrap(P_NCHAR + e % 10, 0..9); knob_set(P_NCHAR, c)`. The characters are None, Wind, Surf, Space, Air,
Tape, Drops, Hail, Stereo Rain and Water.

### 9.4 Encoder steps

| Control | Step |
| --- | --- |
| K1-K4 | 1 per detent, with acceleration and the notch (3.3) |
| SELECT | 1 per detent, raw |
| ALGO | 1 voice, character or walk per detent, raw, wrapping |
| PRESET | 1 piece per detent, raw, wrapping |

### 9.5 Macros (`en_macro`)

| Index | Knob label | Where | Range |
| --- | --- | --- | --- |
| `EN_MAC_FILTER` 0 | Filter | Nachtfahrt K1, Perform K1 | -50..50; shown as "Shut N" / "Off" / "Open N", N = \|v\| × 2 |
| `EN_MAC_DUB` 1 | Wet | Perform K2 | -50..50; shown as ±(\|v\| × 2) |
| `EN_MAC_ENERGY` 2 | Energy | Perform K3; SELECT on Nachtfahrt | -50..50 |
| `EN_MAC_SPACE` 3 | (labelled "Duck") | no page | never sent |

- The engine holds a turned macro until the next section change, then lets its own Motion take over.
- A regrow zeroes all four.
- The UI copy follows the engine through `mac_take` (10.5).

### 9.6 Levels

- The Mix view's K1-K4 write `P_CHORD`, `P_SEQ`, `P_LEAD` and `P_SEQ2`.
- SELECT on a layer page writes that layer's level (9.1).
- The Atmosphere's level `P_CRACKLE` is reachable only by SELECT on its pages. No knob carries it.

### 9.7 Nudges and shakes (`L1228-1359`, `L1406-1420`)

**Buttons and their nudge targets.** `NUDGE_BTN = {ENV, ARP, LFO, SEQ, FX, SAVE}` → layer indices 0..5,
named DRONE, SEQ, LEAD, SEQ 2, SPACE, ATMOS.

| Layer | `NUDGE_PAR` candidates (duplicates removed) | Shake first changes |
| --- | --- | --- |
| 0 Drone | `P_CATT`, `P_CCOL`, `P_CRES`, `P_DREL`, `P_PHASER`, `P_PEDAL` | `P_CVOICE` |
| 1 Sequence | `P_CUTOFF`, `P_DECAY`, `P_SACCENT` | `P_SVOICE` |
| 2 Lead | `P_LTONE`, `P_LATT`, `P_VIB`, `P_GLIDE` | `P_LVOICE` |
| 3 Sequence 2 | `P_S2CUT`, `P_S2DEC` | `P_S2VOICE` |
| 4 Space | `P_ECHO`, `P_FEEDBACK`, `P_REVERB`, `P_SIZE`, `P_CLOUD` | `P_ETIME` (a new echo time) |
| 5 Atmosphere | `P_NCOL`, `P_NGRAIN`, `P_NTUNE`, `P_NROOM` | `P_NCHAR` |

`P_CLOUD` (Space) is on no page: nudges and shakes are the only UI path that changes it.

**Trigger** (`L1406-1420`), for each nudge button with time held `h`:
- `h >= 500` and not used: set used, then `nudge(layer, NUDGE_HAND)`.
- `h >= 1500`, already used (true after the nudge), not yet shaken this hold, and no encoder turned since
  the press: `nudge(layer, NUDGE_SHAKE)`.

**A shake always follows a hand nudge from the same hold.** Holding 1.5 s applies both.

**`nudge(layer, how)`.** The random source is the UI's own LCG: `seed = seed*1664525 + 1013904223` (seed
starts at `0x9E3779B9`, never reseeded), `nrand(n) = (seed >> 8) % n` (`L1247-1252`). It is shared with
Evolve's nudges, so the sequence depends on the history.

1. **SHAKE, Space:** `nt = hi(P_ETIME) + 1 = 6`; `v = (cur + 1 + nrand(nt - 1)) % nt`; `knob_set(P_ETIME, v)`.
2. **SHAKE, other layers:**
   ```
   nv = en_voice_shake_n(VOICE)
   v  = (cur + 1 + nrand(max(nv - 1, 1))) % nv
   if v == cur: v = (v + 1) % nv
   set_voice(VOICE, v)
   ```
3. `en_piece(proj.piece, rec, &pool)`: the current piece's recipe, the anchor.
4. **Count** `n`, picked without repetition from the unique candidates:
   - SHAKE: all of them;
   - HAND: `3 + nrand(2)`, so 3 or 4;
   - EVOLVE: `1 + nrand(2)`, so 1 or 2.
5. **For each pick `p`:**
   ```
   st = (hi - lo) / (p in {P_DECAY, P_CUTOFF, P_FEEDBACK} ? 16 : 8)
   EVOLVE: st = (st + 1) / 2;   SHAKE: st = st * 3 / 2;   st = max(st, 1)
   d  = 1 + nrand(st)
   if v != rec[p] && nrand(hi - lo) < 4 * |v - rec[p]|:  d points toward rec[p]
   else if nrand(2):                                      d = -d
   v  = clamp(v + d);  if changed: knob_set(p, v)         // en_set + dirty; remember the biggest |change|
   ```
6. **EVOLVE stops here.** It shows nothing and changes no view.
7. **HAND or SHAKE** show the header message and a card: "LAYER NUDGE param ±n" or "LAYER SHAKE voice".
   This is skipped when `nudge_quiet`.
8. **View change** (HAND or SHAKE, not in PERFORM, not quiet): `set_view` to the layer's sound page
   (`V_CSND`, `V_PSND`, `V_BSND`, `V_DSND`, `V_SPACE`, `V_NSND`). After a HAND nudge, the knob of the most
   changed parameter is highlighted for 1500 ms.

### 9.8 HOME shake-all (`L1421-1440`)

- **Trigger:** HOME held ≥ 1500 ms, not used, no encoder turned since the press, once per hold.
- **Action:**
  ```
  btn_used |= HOME; nudge_quiet = 1
  nudge(l, SHAKE) for l in {0, 1, 2, 3, 5}   // Drone, Sequence, Lead, Sequence 2, Atmosphere; NOT Space (4)
  nudge_quiet = 0
  ```
  Then one message "SHAKE EVERYTHING" and a card. The view does not change.
- There are no hand nudges first: HOME has no 500 ms action.

### 9.9 Card and message texts

The texts are listed for completeness: they carry no state. They include "NEXT CHORD / on the bar",
"LEAD FORGOTTEN", "LOCK ON/OFF", "SAVED", "RESET piece", "PIECE name", "HOLD ON/OFF", "PLAYING", "RESTING",
"PERFORM ON/OFF", "MIX", "SOLO x" and "x ON/OFF".

---

## 10. Per-frame background logic (`ui_frame`, `L3192-3214`)

These run once per frame (about 60 Hz) in this order, then the drawing. The port must re-implement 10.1-10.5;
only autosave (10.6) is FM-1 only.

### 10.1 Sequence 2 hand flag

`if (ui.s2_hand && !en_s2oct_shift) ui.s2_hand = 0;` (`L3194`).

### 10.2 `prog_take()`

Section 8.3.

### 10.3 `nudge_tick()`: Evolve's automatic nudges (`L1360-1372`)

```
pb = en_phrase_bar                           // 0..7, read from the engine
if pb == last_pb: return;  last_pb = pb      // edge-triggered (file-static, starts at 0xFF)
if pb != 0 || !en_playing || en_frozen || P_EVOLVE <= 40 || ui.lock: return
if nrand(100) < (P_EVOLVE - 40) * 2 / 3:  nudge(nrand(6), NUDGE_EVOLVE)
```
- The chance per eight-bar phrase is 40% at Evolve 100, 20% at 70, and none at 40 or below.
- The layer is chosen at random from all six, Space and Atmosphere included.
- Evolve nudges call `knob_set`, so they **change saved parameters and mark the project dirty**. This is part
  of the instrument's behaviour, not decoration.

### 10.4 `input()`

Section 2.2.

### 10.5 `mac_take()` (`L2734-2744`)

```
for i in 0..2:        // Filter, Wet, Energy only (i < EN_MAC_SPACE)
    v = en_macro_now(i)
    if v != EN_MAC_BUSY && v != ui.mac[i]: ui.mac[i] = v
```
The engine moves Filter, Wet and Energy by itself with Motion, and returns them to 0 at a regrow. The knobs
follow. While a UI turn is still in the queue, `en_macro_now` returns BUSY, so a turn in flight is never
undone.

### 10.6 `autosave()`

Section 7.7.

### 10.7 Drawing, `leds()`, then the screen reader

`leds()` is a pure function of state (13.3). Of the drawing code, only `echo_take()` (`L1918-1922`) writes
engine memory: it zeroes `en_echo_pk` after reading it. That variable is a peak-hold readout.

---

## 11. Saved state (`app.h L19-37`, `project.c`)

**`project_t`**, one flash object of at most 3840 bytes:

| Field | Type | Content |
| --- | --- | --- |
| `magic` | u32 | `0x314C5242` ("BRL1") |
| `format` | u32 | 10 |
| `par[131]` | i16 | the playing piece's parameters, all of them, `P_TUNE` and `P_MIDI` included (these act as the global values) |
| `pool` | u16 | the playing piece's scale, relative to the root |
| `leds` | u8 | Lights 0/1/2 |
| `piece` | u8 | the playing piece, 0..9 |
| `screen` | u8 | the main screen's view mode (unused in NACHTFAHRT's UI: always 0 from recipes) |
| `rsv0` | u8 | reserved |
| `slot[10]` | `slot_t` | each piece as last left |
| `rsv[16]` | u8 | reserved |

**`slot_t`:**

| Field | Type | Content |
| --- | --- | --- |
| `par[131]` | i16 | the piece's parameters; its Tune and MIDI out are copied in but never read back (`take` skips them) |
| `pool` | u16 | the piece's scale |
| `pings[82]` | u8 | its two patterns from `en_seq_get`: degrees 16+16, gate, oct and acc 2 bytes each for both lanes, Density's order 16+16 (`amb.c L1508-1525`) |
| `pings_ok` | u8 | 0 means no patterns kept (a recipe or a reset): grown afresh |
| `screen` | u8 | as above |

**Inside `par[]`:**
- the chord slots `P_CR*`, `P_CQ*` and `P_CX*`, mirrored from the engine;
- the walk character and Loop, `P_PMODE`;
- the Pattern lanes `P_SGATE0..P_SSKIP0` and `P_SACC0/1`, plus `P_SMODE`;
- every value that nudges and Evolve changed.

**Not saved:**
- `ui.mutes`, `ui.solo` and `ui.moves`;
- the macros `ui.mac`, and the engine's macro positions;
- `ui.fx`, `ui.bfx` and `ui.mv`;
- Lock and Hold (`en_frozen`);
- the playing state, which always starts playing;
- the view, `ui.slot` and `ui.lane`;
- `oct_home`, which is derived;
- the Lead's memory and theme (the keys' theme, its ideas and Memory's motifs), which live in the engine
  only;
- Motion's Sequence 2 octave shift;
- the nudge LCG state.

**Write order** (`project_save`): `keep()` first, which copies `proj.par`, `pool`, `screen` and
`en_seq_get` into `slot[piece]` with `pings_ok = 1`. Then the whole struct is written.

**Re-apply order on load:** section 7.6. It is `en_set` for P 0..130 in index order, then `en_pool`, then
`en_seq(pings or NULL)`, with no regrow at boot. A piece change additionally does `en_seed` then
`en_regrow` (7.4); a reset does `en_regrow` only (7.5).

- Is the order significant? The `en_set` loop runs before `en_pool`, and `apply(P_ROOT)` re-voices with the
  old pool until `C_POOL` arrives in the same drain. `apply(P_MODE)` has no effect. Within one drain the end
  state is the same, so following this order is enough.
- `en_seq` must come before `en_regrow`, because the engine re-takes the kept patterns after each regrow
  (`amb.c L4288`).

---

## 12. Hidden state the port must carry

| State | Where | Effect |
| --- | --- | --- |
| `ui.btn_used`, `ui.down_t[]`, `ui.turn_t` | `L230-234` | tap and hold arbitration (2.2) |
| `shaken`, `home_shaken`, `edit_new`, `ui.reset_done` | statics in `input()`, `L947` | once-per-hold latches |
| `held[16]`, `held_t[16]` | `turn()` statics | the notch counters per encoder |
| `enc_t[]`, `enc_run[]` | `ui` | acceleration |
| `nudge_seed` | `L1247` | the nudge and Evolve LCG |
| `last_pb` | `nudge_tick` static | phrase edge detection |
| `ui.prog_gen` | | the `prog_take` generation |
| `ui.s2_hand` | | Sequence 2 octave display |
| `ui.oct_home[]` | | OCT LED homes |
| `ui.mac[]` | | macro knob values (mirrored) |
| `ui.mutes`, `ui.solo`, `ui.moves`, `ui.fx`, `ui.bfx`, `ui.mv`, `ui.lock`, `ui.perf` | | performance state (6.5) |
| `ui.slot`, `ui.lane`, `ui.view` | | page state |
| `ui.dirty`, `ui.act_t` | | autosave (FM-1 only) |

---

## 13. Screen and LED readouts worth mirroring

### 13.1 Header and main screen

| Readout | Source |
| --- | --- |
| Piece name | `en_piece_name(proj.piece)` |
| Tempo, or "EXT" | `proj.par[P_TEMPO]`; `en_ext` (`L1830`) |
| Beat light | `(en_bars / 4) & 1` while `en_playing`; a play triangle while resting (`L1856-1866`) |
| Unsaved dot | `ui.dirty` |
| Key and mode | `P_ROOT`, `mode_name()` ("Custom" if the pool is not a mode) |
| Arc section | `EN_SEC_NAME[en_section]` (EMERGE, FLOW, SOLO, BRIDGE) (`L2429`) |
| Bars of the phrase | `en_phrase_bar` 0..7; bars 3 and 7 accented unless Lock (`L2432`) |
| "hold" | `en_frozen` |
| "lock" | `ui.lock` |
| "SOLO" | `ui.solo != 0` |
| Chord name | if `en_chord_slot >= 0` and that slot is written: `slot_label(en_chord_slot)`. Otherwise `en_chord_name({en_chord_root, en_chord[0..3] % 12}, 5)` (`L1909-1914`). A new name flashes cyan for 900 ms |
| Loop position "n/m" | `en_prog_pos + 1` / `prog_count()`, when Loop is on (`L2420`) |
| Chord progress ring | `en_drift_bars(P_DRIFT)` bars; progress `(round(en_drift * bars) + (en_drum_pos[0] + 1) / 16) / bars`; frozen while `en_frozen`; none at Drift Hold (`L2024-2035`) |
| Level meters (5 layers) | `en_layer_act[0..4]`; the filter tick per layer `clamp(en_fo[i], -4.5, 4.5) * 6` px (`L2088-2093`) |
| Orbits | new dots on changes of `en_seq_hits[l]` and `en_seq_reps[l]` (repeats); red while frozen (`L1982-1997`) |
| Echo meter | `en_echo_pk` (read, then zeroed) |
| Lanes | lengths `P_SEQLEN` / `P_SEQ2LEN`; cursor `en_loop_pos[0/1]` while playing; steps from `en_lane[]` / `en_seq_lane[]` (`L2115-2178`) |
| Pattern page grid | `lane_steps(lane)`, accents, cursor `en_loop_pos[0]` (`L2178-2214`) |
| Chords page slots | the 8 labels; the playing slot `en_prog_pos` (`L2216-2245`) |
| Filter motion text | on a sound page: "MOVING DARKER/BRIGHTER, ±x.x OCT" if `\|en_fo[layer]\| >= 0.1`; "moving" if heading further out (`(en_fo > 0) == (en_fo_dir > 0)`), otherwise "DARKER"/"BRIGHTER" (`L2471-2480`) |
| Density drift text | on the Sequence page: "MOVING DENSER/SPARSER, ±n" if `\|en_dd_shift\| >= 3`, using `en_dd_dir` (`L2486-2491`) |
| Level ring | while SELECT turns: `proj.par[LAYER_LEVEL[l]]`; "MUTED" if the layer is not heard |
| HOME Energy bar | `ui.mac[2]` for 1200 ms after a SELECT turn |
| Setup header | CPU now and peak, xruns, battery (device only) |
| Effects grid | while `grid_up()`: 16 cells, held cells lit (`ui.fx`) |

### 13.2 Knob row (`L2515-2636`)

| Readout | Source |
| --- | --- |
| Value text | `knob_text()` (`L445-504`); `en_param_text` for parameters; a leading '+' is dropped |
| Switches (range < 10) | drawn as pips; a ring otherwise; centred values (lo < 0) fill from the top |
| Voice name or walk name | on layer and Chords pages, shown on the rule |
| Brightness dot (the filter motion) | on sound pages: knob `bright_of(view)`, always K1 (`L654-678`). Heard value `clamp(round(par + en_fo[layer] * per_oct))`; per octave: Drone Colour 25, Sequence Cutoff 18.18, Lead Tone 25, Sequence 2 Cutoff 18.18, Atmosphere Colour 50. The Atmosphere's dot is on `V_NSND2`. A second arc runs from the set value to the heard value |
| Density dot | on the Sequence page: `clamp(P_DENSITY + en_dd_shift, 0, 100)` (`L701-705`, `L2617`) |
| Motion marks | at 40 and 70 on its ring |
| Sequence 2 Octave | shows home + `en_s2oct_shift` (unless `ui.s2_hand`) |

### 13.3 LEDs (`leds()`, `L2636-2701`)

**Buttons:**
- The page's button (`VIEW_BTN[page_of(view)]`) is steady on first pages and on Arc, and blinks with a
  300 ms half period on sub-pages, Key, Echo, Atmos sound and Atmos move.
- Chords, Perform and Mix light no page button.
- PLAY is lit while `en_playing`; REC while `en_frozen`; SEL while in PERFORM.
- OCT+ and OCT- light relative to `oct_home` (4.4).

**Keys**, by priority:
1. Pattern page (not PERFORM, no grid): the lane's steps.
2. Chords page: the slot's root keys.
3. The grid.
4. PERFORM (5.7).
5. Otherwise, if `proj.leds != 0`: the Lead's degree key and the black keys unless held.

`plat_glow(proj.leds == 1)` makes unlit buttons glow dimly.

---

## 14. Parameter table (generated from `en_param_info` / `en_param_text`)

The table was generated by a scratch harness that linked `amb.c`. "Discrete values" lists `en_param_text` for
ranges of 21 values or fewer. "Pages" lists where the UI exposes the parameter.

| # | Enum | Engine name | lo | hi | def | Discrete values | Pages |
|---|---|---|---|---|---|---|---|
| 0 | P_CHORD | Chord | 0 | 100 | 60 | | Drone level: SELECT; Mix K1 |
| 1 | P_SEQ | Sequence | 0 | 100 | 70 | | SELECT on Seq pages; Mix K2 |
| 2 | P_LEAD | Lead | 0 | 100 | 40 | | SELECT on Lead pages; Mix K3 |
| 3 | P_SEQ2 | Sequence 2 | 0 | 100 | 60 | | Seq2 K4; SELECT; Mix K4 |
| 4 | P_DENSITY | Density | 0 | 100 | 60 | | Sequence K1 |
| 5 | P_EVOLVE | Evolve | 0 | 100 | 40 | | Nachtfahrt K2 |
| 6 | P_DRIFT | Drift | 0 | 6 | 4 | 2/4/8/16/32/64 bars, Hold | Nachtfahrt K4 |
| 7 | P_TAPA | Repeat A | 0 | 100 | 100 | | Repeats K1 |
| 8 | P_ECHO | Echo | 0 | 100 | 55 | | Space K1 |
| 9 | P_ETIME | Echo time | 0 | 5 | 3 | 1/8, 1/8., 1/4, 1/4., 1/2, 1 bar | Space K2 |
| 10 | P_REVERB | Reverb | 0 | 100 | 35 | | Space K4 |
| 11 | P_FEEDBACK | Feedback | 0 | 100 | 60 | | Space K3 |
| 12 | P_CUTOFF | Cutoff | 0 | 100 | 45 | | Seq sound K1 |
| 13 | P_DECAY | Decay | 0 | 100 | 35 | | Seq sound K2 |
| 14 | P_SWEEP (= P_MW1) | Sweep | 0 | 100 | 50 | | Seq move K1; Perform K4 |
| 15 | P_MOTION | Motion | 0 | 100 | 50 | | Nachtfahrt K3 |
| 16 | P_TUNE | Tune | -50 | 50 | 0 | | Setup K1 (global) |
| 17 | P_MIDI | MIDI out | 0 | 1 | 1 | Off, On | Setup K2 (global) |
| 18 | P_TEMPO | Tempo | 60 | 160 | 96 | | Key K1 |
| 19 | P_ROOT | Root | 0 | 11 | 5 | C..B | Key K2 |
| 20 | P_MODE | Mode | 0 | 7 | 1 | Major, Dorian, Phrygian, Lydian, Mixolyd., Minor, Pent maj, Pent min | Key K3 (through `MODE_ORDER`) |
| 21 | P_SEQOCT | Seq oct | -2 | 2 | 0 | -2..+2 | Sequence K2; OCT |
| 22 | P_CVOICE | Chord voice | 0 | 5 | 0 | Organ, Strings, Horn, Saws, Glass, Choir | ALGO on Drone pages |
| 23 | P_SEQ2OCT | Seq2 oct | -2 | 1 | -1 | -2..+1 | Seq2 K2; OCT |
| 24 | P_SEQGRID | Grid | 0 | 1 | 0 | 8ths, 16ths | Sequence K4 |
| 25 | P_TRANS | Transpose | 0 | 2 | 0 | Parallel, Diatonic, Bed | Key K4 |
| 26 | P_TAPAT | Repeat A time | 0 | 5 | 1 | as Echo time | Repeats K2 |
| 27 | P_TAPB | Repeat B | 0 | 100 | 0 | | Repeats K3 |
| 28 | P_SEQLEN | Length | 3 | 16 | 8 | | Sequence K3 |
| 29 | P_CRACKLE | Atmosphere (level) | 0 | 100 | 25 | | SELECT on Atmos pages |
| 30 | P_TAPBT | Repeat B time | 0 | 5 | 2 | as Echo time | Repeats K4 |
| 31 | P_SIZE | Size | 0 | 100 | 55 | | Echo K4 |
| 32 | P_CLOUD | Cloud | 0 | 100 | 30 | | none (Space nudge only) |
| 33 | P_SVOICE | Seq voice | 0 | 16 | 0 | 2600 ... Piano (9.2) | ALGO on Seq pages |
| 34 | P_LVOICE | Lead voice | 0 | 9 | 0 | Odyssey ... Piano | ALGO on Lead pages |
| 35 | P_S2VOICE | Seq2 voice | 0 | 17 | 1 | 2600 ... Piano | ALGO on Seq2 pages |
| 36 | P_CATT | Attack | 0 | 100 | 40 | | Drone K4 |
| 37 | P_CCOL | Colour | -50 | 50 | 0 | | Drone sound K1 |
| 38 | P_CRES | Resonance | -50 | 50 | 0 | | Drone K2 |
| 39 | P_DREL | Release | 0 | 100 | 50 | | Drone sound K2 |
| 40 | P_PEDAL | Pedal | 0 | 100 | 70 | | Drone K1 |
| 41 | P_SQREV | Seq reverb | 0 | 100 | 50 | | Seq sound K3 |
| 42 | P_SQECHO | Seq echo | 0 | 100 | 50 | | Seq sound K4 |
| 43 | P_LDREV | Lead reverb | 0 | 100 | 90 | | Lead sound K3 |
| 44 | P_LOCT | Lead oct | -2 | 1 | 0 | -2..+1 | Lead K2; OCT |
| 45 | P_VIB | Vibrato | 0 | 100 | 40 | | Lead K4 |
| 46 | P_LDECHO | Lead echo | 0 | 100 | 90 | | Lead sound K4 |
| 47 | P_GLIDE | Glide | 0 | 100 | 25 | | Lead K3 |
| 48 | P_MT4 | Atmos time | 0 | 100 | 50 | | Atmos move K2 |
| 49 | P_DRREV | Drone reverb | 0 | 100 | 50 | | Drone sound K3 |
| 50 | P_DRECHO | Drone echo | 0 | 100 | 50 | | Drone sound K4 |
| 51 | P_S2ECHO | Seq2 echo | 0 | 100 | 50 | | Seq2 sound K4 |
| 52 | P_MW0 | Drone width | 0 | 100 | 20 | | Drone move K1 |
| 53 | P_MT0 | Drone time | 0 | 100 | 50 | | Drone move K2 |
| 54 | P_MR0 | Drone rate | 0 | 100 | 30 | | Drone move K3 |
| 55 | P_MI0 | Drone intro | 0 | 64 | 0 | | Drone move K4 |
| 56 | P_S2DEC | Decay 2 | 0 | 100 | 35 | | Seq2 sound K2 |
| 57 | P_MT1 | Seq time | 0 | 100 | 50 | | Seq move K2 |
| 58 | P_MR1 | Seq rate | 0 | 100 | 30 | | Seq move K3 |
| 59 | P_MI1 | Seq intro | 0 | 64 | 0 | | Seq move K4 |
| 60 | P_MW2 | Lead width | 0 | 100 | 20 | | Lead move K1 |
| 61 | P_LATT | Lead attack | 0 | 100 | 10 | | Lead sound K2 |
| 62 | P_MT2 | Lead time | 0 | 100 | 50 | | Lead move K2 |
| 63 | P_MR2 | Lead rate | 0 | 100 | 30 | | Lead move K3 |
| 64-71 | P_CR0..7 | Root 1..8 | 0 | 11 | 0 | C..B | Chords (8.4) |
| 72-79 | P_CQ0..7 | Kind 1..8 | 0 | 6 | 0 | Empty, Major, Minor, Sus2, Sus4, Dim, Aug | Chords K2 |
| 80-87 | P_CX0..7 | Type 1..8 | 0 | 6 | 0 | Triad, 6, 7, Maj7, 9, 11, Add9 | Chords K3 |
| 88 | P_PMODE | Play | 0 | 6 | 1 | 0-3 Endless, 4-6 Loop (bits 0-1 walk) | Chords K4; ALGO on Chords |
| 89 | P_SGATE0 | Steps | -32768 | 32767 | 0 | bitmask | Pattern lane 0 |
| 90 | P_SGATE1 | Steps 2 | ±32k | | 0 | bitmask | Pattern lane 1 |
| 91 | P_SOCT0 | Octaves | ±32k | | 0 | bitmask | Pattern lane 2 |
| 92 | P_SOCT1 | Octaves 2 | ±32k | | 0 | bitmask | Pattern lane 3 |
| 93 | P_SSKIP0 | Skips | ±32k | | 0 | bitmask | Pattern lane 4 |
| 94 | P_SACC0 | Accents | ±32k | | 0 | bitmask | Pattern, black + white (Sequence lanes) |
| 95 | P_SACC1 | Accents 2 | ±32k | | 0 | bitmask | Pattern, black + white (Seq2 lanes) |
| 96 | P_MI3 | Seq2 intro | 0 | 64 | 0 | | Seq2 move K4 |
| 97 | P_MI2 | Lead intro | 0 | 64 | 0 | | Lead move K4 |
| 98 | P_MW3 | Seq2 width | 0 | 100 | 20 | | Seq2 move K1 |
| 99 | P_SCH0 | Chance | 0 | 100 | 100 | | Pattern K3 (lanes 0, 2, 4) |
| 100 | P_SCH1 | Chance 2 | 0 | 100 | 100 | | Seq2 K1; Pattern K3 (lanes 1, 3) |
| 101 | P_LDIN | Lead in | 0 | 65 | 16 | 65 = Off | Arc K1 |
| 102 | P_MT3 | Seq2 time | 0 | 100 | 50 | | Seq2 move K2 |
| 103 | P_MR3 | Seq2 rate | 0 | 100 | 30 | | Seq2 move K3 |
| 104 | P_SACCENT | Accent | 0 | 100 | 50 | | Pattern K2 |
| 105 | P_SMODE | Seq play | 0 | 1 | 0 | Auto, Pattern | Pattern K4 |
| 106 | P_NCOL | Noise tone | -50 | 50 | 0 | | Atmos sound K1 |
| 107 | P_NGRAIN | Noise density | -50 | 50 | 0 | | Atmosphere K1 |
| 108 | P_NROOM | Air | -50 | 50 | 0 | | Atmosphere K4 |
| 109 | P_NDRIFT | Noise drift | -50 | 50 | 0 | | Atmosphere K3 |
| 110 | P_NCHAR | Noise | 0 | 9 | 0 | None, Wind, Surf, Space, Air, Tape, Drops, Hail, Stereo Rain, Water | ALGO on Atmos pages |
| 111 | P_PHASER | Phaser | 0 | 100 | 40 | | Drone K3 |
| 112 | P_S2CUT | Cutoff 2 | 0 | 100 | 40 | | Seq2 sound K1 |
| 113 | P_LDENS | Phrases | 0 | 100 | 50 | | Lead K1 |
| 114 | P_S2REV | Seq2 reverb | 0 | 100 | 50 | | Seq2 sound K3 |
| 115 | P_LTONE | Lead tone | 0 | 100 | 50 | | Lead sound K1 |
| 116 | P_SEQ2LEN | Length 2 | 3 | 16 | 7 | | Seq2 K3 |
| 117 | P_EBLUR | Blur | 0 | 100 | 50 | | Echo K1 |
| 118 | P_ETONE | Echo tone | 0 | 100 | 50 | | Echo K2 |
| 119 | P_EWIDTH | Width | 0 | 100 | 50 | | Echo K3 |
| 120 | P_NTUNE | Ring | 0 | 100 | 50 | | Atmos sound K2 |
| 121 | P_NWIDTH | Noise width | 0 | 100 | 50 | | Atmosphere K2 |
| 122 | P_NPLATE | Noise plate | 0 | 100 | 50 | | Atmos sound K3 |
| 123 | P_NECHO | Noise echo | 0 | 100 | 50 | | Atmos sound K4 |
| 124 | P_MW4 | Atmos width | 0 | 100 | 20 | | Atmos move K1 |
| 125 | P_NLFO | Noise LFO | 0 | 100 | 0 | | none |
| 126 | P_ARC | Arc | 0 | 100 | 0 | | Arc K3 |
| 127 | P_MR4 | Atmos rate | 0 | 100 | 30 | | Atmos move K3 |
| 128 | P_MI4 | Atmos intro | 0 | 64 | 0 | | Atmos move K4 |
| 129 | P_BRIDGE | Bridge | 0 | 100 | 40 | | Arc K2 |
| 130 | P_LMEM | Memory | 0 | 100 | 50 | | Arc K4 |

Pieces 0-9: Timewind, Wahnfried, Phaedra, Rubycon, Stratosfear, Sunrain, Mirage, Cyborg, Sakura Rain, Umeda.

Mode pools, bit i = root + i semitones:

| Mode | Pool | Mode | Pool |
| --- | --- | --- | --- |
| Major | 0xAB5 | Mixolydian | 0x6B5 |
| Dorian | 0x6AD | Minor | 0x5AD |
| Phrygian | 0x5AB | Pent maj | 0x295 |
| Lydian | 0xAD5 | Pent min | 0x4A9 |

Parameters on no page: `P_CLOUD` (nudges only) and `P_NLFO` (recipes only). The four `P_N*` -50..50
parameters and `P_CCOL`/`P_CRES` are offsets from the voice's own sound (0 = as designed).

---

## 15. Discrepancies, quirks and suspected upstream bugs

**Q1. A Chords-page key on a gap loses the key's pitch** (`L938-946`). This finding comes from tracing the
code by hand, not from a run: the host simulator cannot be built from `tests/upstream`, because `gfx.c` and
`announce.c` are not there.
- On a gap slot (CQ 0, CR 0), `knob_set(P_CR0, pc)` makes the slot `CQ == 0 && CR != 0`, which is a *walk*
  slot whose CR now reads as degree + 1.
- Next, `knob_set(K_CQ, MINOR)` compares against the walk chord's kind (`knob_get`). If it differs,
  `slot_write` replaces CR with `en_walk_chord`'s root for degree `(pc - 1) % steps`, and then CQ becomes
  Minor. If it is equal, nothing more happens and the slot stays a walk slot.
- Either way the chord's root is not the key's pitch class, unless the key is a C, where `pc == 0` leaves the
  slot a gap and makes a C minor.
- The manual and the comment intend "a gap becomes a minor chord on the key's note". To get that, write CQ
  (Minor) before CR, or write both directly.

**Q2. `en_piece_theme()` is never called by the app** (ui.c and project.c).
- Umeda's written theme (`amb.c L3085-3108`, `theme_pc == 9`) therefore never arms on the FM-1 build at this
  pin, although amb.h and the manual describe it.
- The intended call is probably `en_piece_theme(proj.piece)` before `en_regrow()` in `set_piece`, the reset
  and the boot. The port must decide.

**Q3. SEL does not leave PERFORM**, although the manual (§10) says "SEL again, or a page's button, leaves".
In the code, SEL in PERFORM only returns Mix to Perform (`L1090-1096`).

**Q4. HOME shake-all skips Space** (`ALL = {0,1,2,3,5}`, `L1429`), although its card says "every layer, and
the space".

**Q5. Nudges cover six buttons, not four.** FX (Space) and SAVE (Atmosphere) also nudge and shake. The
manual mentions only ENV, ARP, LFO and SEQ.

**Q6. A shake always includes a hand nudge.** The 500 ms nudge fires first, so a 1.5 s hold applies both.

**Q7. Key releases are swallowed while a grid effect or REC is held** (`L1465-1469`).
- While `ui.fx != 0`, every white key release goes to `fx_key()` and stops there.
- In PERFORM, releasing a move key (white 6-10) or the Mix key (white 5) at that moment never reaches
  `perf_key`. The move stays on, or the view stays on Mix, until the key is pressed and released again or
  PERFORM is left.
- The same happens with **REC merely held** (`L1468-1469`, `ui.fx == 0`): every key release while REC is down
  stops before `perf_key`. Example: hold a move key or D4 (Mix), press REC, release the key. The move stays
  on, or the view stays on Mix.
- The Lead's release and the black keys' releases are unaffected: they are sent earlier (`L1454-1464`).

**Q8. The external clock timeout counts render calls** (`amb.c L6350`: 100 calls).
- With the MPC's block size not 256, the time to fall back to the internal clock scales with it: 128 frames
  gives 0.29 s, 512 frames 1.16 s.
- It is the same for `en_render(n)` calls with n < 256: the timeout is per call, not per sample.

**Q9. Boot differs from a piece change.** Boot calls no `en_seed` and no `en_regrow`, so its dice are
`en_init`'s fixed seeds, so every power-on starts with the same random sequence. A GLO reset has no
`en_seed` either: its regrow continues from the engine's current random state. Only PLAY (to play) and a
piece change stir in `plat_ms()`.

**Q10. Dead code and dead fields:**
- `toggle_note()` (keys toggling scale notes) is never called;
- `chords_from` and `noise_from` are written, never read;
- `perf_from` is overridden at once;
- `K_SPACE` / `EN_MAC_SPACE` is on no page;
- `K_CPU` is on no page;
- `proj.screen` and `slot.screen` are kept and saved, but `ui.c` never reads them (no `.screen` reference).

**Q11. Stale header comment** (`L5-19`). It describes DUB-1 (keys toggle the pool, the "FX ENV LFO GLO" page
names, GLO held 1.5 s while SAVE holds the reset). The comments at `L758` ("MIDI notes in still change the
scale") and `L1226` ("SAVE held"; also `L235`) are also stale. The code is authoritative.

**Q12. Manual counts.** It says "eight" pieces in places (§2, §3 PRESETS, §12); there are 10. It mentions
PANIC as a user action; there is no PANIC control.

**Q13. REC's tap reads the engine flag**: `en_freeze(!en_frozen)`. `en_frozen` updates one block later, so
two taps within one block send the same value twice.

**Q14. The REC grid is live from the press**, but the grid display and the K1-K4 lockout start at 300 ms. A
key played within 300 ms already makes REC's release skip the Hold toggle.

**Q15. ALGO on the Chords page always calls `en_prog_rewalk()`**, even when the detent count is a multiple
of 3 and the walk character does not change.

**Q16. The level and Energy steps (SELECT) bypass acceleration and the notch.** At 1 per detent, a sweep of
0-100 on a level takes 100 detents.

**Q17. Engine calls the app makes that sit outside the public API:**
- `en_mac_got`, the macro handshake, is used through `en_macro_now()` and is not in amb.h.
- `en_par_get` exists only under `EN_HOST`. VENDOR.md notes that Third Planet uses it for state restore.
- The app writes `en_load` (in amb.h).
- The app zeroes the engine global `en_echo_pk` (`L1922`).
- The UI reads `en_prog_slot` and `en_prog_gen` directly.

**Q18. Thread safety on the MPC.**
- `en_seq_get()` reads the live patterns without the queue, and `project_piece`'s `keep()` calls it from the
  UI thread on the FM-1.
- `en_seq()` writes its pending buffer at once.
- On the MPC, call both from the audio thread (between renders), or accept a torn read.

---

## 16. Gestures needing an MPC equivalent

Every gesture that depends on holding, timing or two controls at once:

1. **REC tap and hold.** A tap toggles Hold (`en_freeze`). A hold makes the white keys the 16-effect grid;
   the display shows at 300 ms.
2. **REC held + white key(s).** Each effect lasts while its key is held. Effects latch past REC's release
   until their keys are released, and several can be held together.
3. **A black key held** (normal playing). 11 momentary effects and moves, active only while held. Black keys
   0-7 combine (OR) with the same effects held on the grid.
4. **A white key held** (normal playing). The Lead note sustains while held: monophonic, last key wins,
   legato over a held key. A figure of 3 or more notes followed by 2 beats of silence becomes the Lead's
   theme. That timing is the engine's, from key down and up events.
5. **EDIT.** A tap opens Chords. A hold of 500 ms or more, released, gives the next chord. A hold of 1.5 s
   with no encoder turn, while held, gives new chords.
6. **SEL.** A tap enters PERFORM (or returns Mix to Perform). Released after 500 ms or more, it toggles
   Lock.
7. **GLO.** A tap selects pages. Released after 500 ms or more, it saves. Held for more than 1.5 s, while
   held, it resets the piece to its recipe without saving.
8. **HOME.** A tap selects pages. Held 1.5 s with no encoder turn, it shakes all of Drone, Sequence, Lead,
   Sequence 2 and Atmosphere.
9. **ENV, ARP, LFO, SEQ, FX, SAVE.** A tap selects pages. Held 500 ms: a nudge, with a view jump to the sound
   page. Held 1.5 s with no encoder turn: a shake (a new voice, echo time or character, plus big steps).
10. **"No encoder turned since the press"** guards gestures 5, 8 and 9 (the shake parts). Turning any encoder
    while holding cancels the pending long action.
11. **OCT- and OCT+ together.** Neither steps. On the FM-1, a 5 s hold enters the bootloader (do not port).
    OCT steps on its release, on Sequence, Lead and Sequence 2 pages.
12. **PERFORM white 5 (D4) held.** The knobs become the Mix levels while held; release returns to the
    Perform knobs.
13. **PERFORM white 6-10 held.** The moves Sweep, Open, Shut, Drop and Echo last while held, and several can
    combine.
14. **PERFORM black 0-4.** Solo toggles, multi-solo allowed. They combine with the mutes into one mask (6.5).
15. **Pattern page: a black key held + a white key.** This toggles the step's accent and forces the step on.
16. **A Chords-page key.** It sets the slot's root from the key's pitch class (needs the Q1 fix). Any key,
    including black keys.
17. **Centred knob notch.** Two detents are held back at 0, with a 700 ms reset, and a turn through zero
    stops at 0. This needs an equivalent on Q-Links or touch knobs (or can be dropped).
18. **Knob acceleration** by detent speed (3.3). On the MPC the Q-Links and the touch screen have their own
    resolution; decide whether to keep this.
19. **SELECT on Nachtfahrt.** Energy is turned with one hand while the other plays the white keys: the
    original's one-hand-each design.
20. **MIDI in notes and clock.** Already MPC-native: map the host's MIDI input to `en_lead_midi`, and the
    host transport or clock to `en_clock` (Q8 for the timeout).
