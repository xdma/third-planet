# Harmony Link v1

Harmony Link is an open MIDI SysEx format. With it, Third Planet tells another program (a sequencer on a computer,
say) what its harmony is doing now and what it will do next: the chord playing, the next chord and when it comes,
the progression's slots, the form and the layers' activity. The other program can send two commands back, a fade
and Hold.

Anyone may implement either side. Golden vectors to test an implementation against are in
[`harmony-link-vectors.txt`](harmony-link-vectors.txt). Third Planet's own codec is `src/link_codec.h` and
`src/link_codec.cpp`, and `tests/test_link_codec.cpp` checks it against those vectors.

## Framing

Every message is one SysEx message:

```
F0 7D 54 50 <ver> <type> <seq> <payload...> F7
```

| Byte | Value | Meaning |
| --- | --- | --- |
| 0 | `F0` | SysEx start |
| 1 | `7D` | the MIDI non-commercial manufacturer ID |
| 2, 3 | `54 50` | "TP" |
| 4 | `01` | the format version: this document is version 1 |
| 5 | type | see the table below |
| 6 | seq | a running 7-bit counter (0..127, then 0 again), kept separately by each sender, so a receiver can spot a lost message |
| 7... | payload | depends on the type |
| last | `F7` | SysEx end |

Every byte between `F0` and `F7` is 7-bit (`00`..`7F`). A value that doesn't fit in 7 bits is split into 7-bit
groups, least significant group first. A sender clamps every field to its range before encoding it.

No message is longer than 160 bytes. A receiver can ignore any SysEx that doesn't start `F0 7D 54 50`.

| Type | Name | Direction | Total length |
| --- | --- | --- | --- |
| `01` | PLAN | Third Planet to the receiver | 146 bytes |
| `02` | EVENT | Third Planet to the receiver | 17 bytes |
| `03` | BYE | Third Planet to the receiver | 8 bytes |
| `10` | FADE | the receiver to Third Planet | 11 bytes |
| `11` | HOLD | the receiver to Third Planet | 9 bytes |

## Encodings

- **byte**: one 7-bit byte, 0..127.
- **offset byte (+k)**: a signed value v sent as v + k, clamped to 0..127. Third Planet uses +64 and +1.
- **14-bit signed**: a value v in -8192..8191, sent as v + 8192 (0..16383) in two bytes: the low 7 bits, then
  the high 7 bits.
- **14-bit unsigned**: a value 0..16383 in two bytes: the low 7 bits, then the high 7 bits.
- **CHORD**: 11 bytes.

  | Offset | Field | Range |
  | --- | --- | --- |
  | 0 | `root` | 0..11 (C..B), or 127 for no chord |
  | 1 | `mask` bits 0..6 | the chord's pitch classes as absolute bits: bit n set means pitch class n (0 = C) sounds |
  | 2 | `mask` bits 7..11 | (bits 5 and 6 of this byte are 0) |
  | 3..10 | `name` | up to 8 ASCII characters, such as `Am9` or `Emaj7#11`, padded with `00`; 8 characters means no `00` |

  No chord is `7F 00 00` and a name of eight `00`. A gap slot holds no chord.

## PLAN (`01`)

The whole state of the harmony, sent when it changes, when the host transport starts, and as a heartbeat. A
receiver should treat each PLAN as complete: nothing in it depends on an earlier one.

How often a PLAN comes:

- A change in anything but `densityShift`, `activity` and `pos16` is sent at once, and so is the PLAN that follows
  an EVENT. At most 20 PLANs a second: of several changes within 50 ms, only the newest state is sent.
- While only `densityShift`, `activity` and `pos16` change (they move all the time while the piece plays), at most
  2 PLANs a second, each with the newest state. So the layers' activity reaches a receiver at about 2 Hz.
- With nothing new, the last PLAN is sent again every 2 seconds: the heartbeat.
- The first PLAN after Link is turned on, or after the connection is made again, is sent at once.

Payload, 138 bytes, in this order:

| Offset | Field | Encoding | Range and meaning |
| --- | --- | --- | --- |
| 0 | `flags` | byte | bit 0 `playing`, bit 1 `held` (Hold is on), bit 2 `locked`, bit 3 `loopMode` (the slots play in turn; 0: Endless), bit 4 `timed` (Third Planet runs on the host's clock) |
| 1 | `piece` | byte | the piece's index |
| 2 | `keyRoot` | byte | 0..11 (C..B) |
| 3 | `keyMode` | byte | the mode's index, 0..7: see [Modes](#modes) |
| 4 | `now` | CHORD | the chord playing |
| 15 | `next` | CHORD | the next chord that will sound |
| 26 | `barsToNext` | offset byte (+64) | bars until `next` sounds, counting the bar playing; -2 while Hold is on, -1 with no next chord or while Drift holds the chord |
| 27 | `barsToChange` | offset byte (+64) | bars to the next harmonic change, counting the bar playing (1: this is the last bar); -1 while Drift holds the chord; while Hold is on it counts down to 0 and stays there |
| 28 | `progPos` | offset byte (+1) | the slot the progression is on, 0..7; -1 none |
| 29 | `progCount` | byte | how many slots the progression runs through in Loop |
| 30 | `walk` | byte | the walk's character: 0 Home, 1 Wander, 2 Free (each more varied than the one before) |
| 31 | 8 slots | 8 x 12 bytes | each: `slotState` (byte: 0 gap, 1 walk, 2 written), then a CHORD |
| 127 | `section` | byte | the form's section: 0 Emerge, 1 Flow, 2 Solo, 3 Bridge |
| 128 | `phraseBar` | byte | the bar within the phrase, 0..7 |
| 129 | `evolveChance` | byte | Evolve's chance, in percent |
| 130 | `densityShift` | offset byte (+64) | Density's slow drift (positive: denser, negative: sparser) |
| 131 | `heardMask` | byte | bit n set: layer n (0..4, see [Layers](#layers)) is heard (not muted, not soloed out); bits 5 and 6 are 0 |
| 132 | `activity` | 5 bytes | how much each layer (0..4, in the same order) sounds now, 0..127 |
| 137 | `pos16` | byte | the host's position when the PLAN was taken, in 16ths since the host's last bar line, 0..15 (see below) |

`pos16` stops at 15. In 4/4 a bar has 16 sixteenths, so it is exact. In a longer bar (5/4, 7/4, 12/8 and so on)
every position from the 16th sixteenth to the bar's end reads 15; in a shorter bar (3/4, 7/8) it never gets that
far. With no position from the host it is 0.

When `timed` is 1, Third Planet's bar lines are the host's, so a receiver can turn "the next chord in N bars" plus
`pos16` into an absolute bar. When `timed` is 0, the plan is untimed: use its harmony only, not its bar counts.

### Modes

`keyMode` is one of the instrument's eight modes. The notes are semitones above `keyRoot`.

| `keyMode` | Mode | Notes |
| --- | --- | --- |
| 0 | Major (Ionian) | 0 2 4 5 7 9 11 |
| 1 | Dorian | 0 2 3 5 7 9 10 |
| 2 | Phrygian | 0 1 3 5 7 8 10 |
| 3 | Lydian | 0 2 4 6 7 9 11 |
| 4 | Mixolydian | 0 2 4 5 7 9 10 |
| 5 | Minor (Aeolian) | 0 2 3 5 7 8 10 |
| 6 | Major pentatonic | 0 2 4 7 9 |
| 7 | Minor pentatonic | 0 3 5 7 10 |

The player can change single notes of the scale on the instrument. `keyMode` then still names the mode the scale
started from, so take `keyRoot` and `keyMode` as the key's name, and the notes to play from the chords' `mask`.

### Layers

`heardMask`, `activity` and an EVENT's mute or solo count the five layers in this order:

| Layer | Name |
| --- | --- |
| 0 | Drone |
| 1 | Sequence |
| 2 | Lead |
| 3 | Sequence 2 |
| 4 | Atmosphere |

## EVENT (`02`)

One action, sent at once. A PLAN follows as soon as the action's effect shows in the state.

Payload, 9 bytes:

| Offset | Field | Encoding | Range and meaning |
| --- | --- | --- | --- |
| 0 | `who` | byte | 0 the player, 1 Evolve, 2 a piece change |
| 1 | `what` | byte | 0 parameter, 1 voice, 2 Hold, 3 Lock, 4 play, 5 forget, 6 next chord, 7 new chords, 8 shake all, 9 reset, 10 mute, 11 solo, 12 nudge, 13 shake, 14 piece |
| 2 | `layer` | offset byte (+1) | -1 none. For mute and solo: the layer, 0..4 (see [Layers](#layers)). For nudge, shake, parameter and voice: the nudge's target, 0 Drone, 1 Sequence, 2 Lead, 3 Sequence 2, 4 Space, 5 Atmosphere |
| 3 | `param` | 14-bit signed | the parameter, -1 none |
| 5 | `from` | 14-bit signed | the value before (for on/off actions: 0 or 1) |
| 7 | `to` | 14-bit signed | the value after |

EVENTs are the player's actions and piece changes. What a command from the receiver does is not sent back as an
EVENT: after a HOLD command there is no "Hold" EVENT, only a PLAN with `held` changed (see [HOLD](#hold-11-to-third-planet)).

## BYE (`03`)

No payload. Third Planet sends it when Link is turned off or the plugin closes. No more PLANs follow until Link
is on again: the last PLAN is no longer live, so a receiver should not schedule from its bar counts.

## FADE (`10`), to Third Planet

Ramp Third Planet's output gain from where it is to `level / 127` over `bars` bars of the host clock.

| Offset | Field | Encoding | Range |
| --- | --- | --- | --- |
| 0 | `level` | byte | 0..127: the target gain is `level / 127` (0 silent, 127 full) |
| 1 | `bars` | 14-bit unsigned | 0..16383 (0: at once) |

The level goes back to full, at once, when the host transport starts, when Link is turned off and when a project
is loaded. A FADE is not kept with the project.

## HOLD (`11`), to Third Planet

Hold on or off, the same as the player's Hold. Third Planet does not answer a HOLD with an EVENT: the next PLAN's
`held` flag shows it. The same goes for a hold that Third Planet releases itself when the host transport starts (it
releases only a hold that a HOLD command set, never the player's): a PLAN with `held` 0, no EVENT. When the player
turns Hold on or off by hand, the EVENT comes as usual.

| Offset | Field | Encoding | Range |
| --- | --- | --- | --- |
| 0 | `on` | byte | 0 off, 1 on |

## Receiving commands

Third Planet accepts a FADE or HOLD only when all of these hold; otherwise it ignores the message:
- it starts `F0 7D 54 50 01` and ends `F7`, and every byte between is `00`..`7F`;
- its type is `10` or `11`, and its length is exactly the one in the table above (11 or 9 bytes);
- for HOLD, `on` is 0 or 1.

`seq` is not checked. A future version will have a different version byte; a version 1 receiver ignores it.
