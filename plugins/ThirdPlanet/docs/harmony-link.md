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

The whole state of the harmony, sent whenever it changes, when the host transport starts, and as a heartbeat. A
receiver should treat each PLAN as complete: nothing in it depends on an earlier one.

Payload, 138 bytes, in this order:

| Offset | Field | Encoding | Range and meaning |
| --- | --- | --- | --- |
| 0 | `flags` | byte | bit 0 `playing`, bit 1 `held` (Hold is on), bit 2 `locked`, bit 3 `loopMode` (the slots play in turn; 0: Endless), bit 4 `timed` (Third Planet runs on the host's clock) |
| 1 | `piece` | byte | the piece (0-based) |
| 2 | `keyRoot` | byte | 0..11 (C..B) |
| 3 | `keyMode` | byte | the mode's index |
| 4 | `now` | CHORD | the chord playing |
| 15 | `next` | CHORD | the next chord that will sound |
| 26 | `barsToNext` | offset byte (+64) | bars until `next` sounds, counting the bar playing; -2 while Hold is on, -1 with no next chord or while Drift holds the chord |
| 27 | `barsToChange` | offset byte (+64) | bars to the next harmonic change, counting the bar playing (1: this is the last bar); -1 while Drift holds the chord; while Hold is on it counts down to 0 and stays there |
| 28 | `progPos` | offset byte (+1) | the slot the progression is on, 0..7; -1 none |
| 29 | `progCount` | byte | how many slots the progression runs through in Loop |
| 30 | `walk` | byte | the walk's character (its index) |
| 31 | 8 slots | 8 x 12 bytes | each: `slotState` (byte: 0 gap, 1 walk, 2 written), then a CHORD |
| 127 | `section` | byte | the form's section: 0 Emerge, 1 Flow, 2 Solo, 3 Bridge |
| 128 | `phraseBar` | byte | the bar within the phrase, 0..7 |
| 129 | `evolveChance` | byte | Evolve's chance, in percent |
| 130 | `densityShift` | offset byte (+64) | Density's slow drift (positive: denser, negative: sparser) |
| 131 | `heardMask` | byte | bit n set: layer n (0..4) is heard (not muted, not soloed out) |
| 132 | `activity` | 5 bytes | how much each layer (0..4) sounds now, 0..127 |
| 137 | `pos16` | byte | the host's position when the PLAN was taken, in 16ths since the host's last bar line, 0..15 |

When `timed` is 1, Third Planet's bar lines are the host's, so a receiver can turn "the next chord in N bars" plus
`pos16` into an absolute bar. When `timed` is 0, the plan is untimed: use its harmony only, not its bar counts.

## EVENT (`02`)

One action, sent at once. A PLAN follows as soon as the action's effect shows in the state.

Payload, 9 bytes:

| Offset | Field | Encoding | Range and meaning |
| --- | --- | --- | --- |
| 0 | `who` | byte | 0 the player, 1 Evolve, 2 a piece change |
| 1 | `what` | byte | 0 parameter, 1 voice, 2 Hold, 3 Lock, 4 play, 5 forget, 6 next chord, 7 new chords, 8 shake all, 9 reset, 10 mute, 11 solo, 12 nudge, 13 shake, 14 piece |
| 2 | `layer` | offset byte (+1) | the layer or nudge target, -1 none |
| 3 | `param` | 14-bit signed | the parameter, -1 none |
| 5 | `from` | 14-bit signed | the value before (for on/off actions: 0 or 1) |
| 7 | `to` | 14-bit signed | the value after |

## BYE (`03`)

No payload. Third Planet sends it when Link is turned off or the plugin closes. A receiver should forget the last
PLAN.

## FADE (`10`), to Third Planet

Fade Third Planet's output to `level` over `bars`.

| Offset | Field | Encoding | Range |
| --- | --- | --- | --- |
| 0 | `level` | byte | 0 (silent)..127 (full) |
| 1 | `bars` | 14-bit unsigned | 0..16383 bars (0: at once) |

## HOLD (`11`), to Third Planet

Hold on or off, the same as the player's Hold.

| Offset | Field | Encoding | Range |
| --- | --- | --- | --- |
| 0 | `on` | byte | 0 off, 1 on |

## Receiving commands

Third Planet accepts a FADE or HOLD only when all of these hold; otherwise it ignores the message:
- it starts `F0 7D 54 50 01` and ends `F7`, and every byte between is `00`..`7F`;
- its type is `10` or `11`, and its length is exactly the one in the table above (11 or 9 bytes);
- for HOLD, `on` is 0 or 1.

`seq` is not checked. A future version will have a different version byte; a version 1 receiver ignores it.
