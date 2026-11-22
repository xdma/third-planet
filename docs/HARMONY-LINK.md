# Harmony Link

Harmony Link is an optional feature of Third Planet that is **compiled out of the release build**. This page
says what it is, why it exists and how to build Third Planet with it.

## What it is

Third Planet composes its own harmony as it plays: a chord progression, a form with sections, layers that come
and go. Harmony Link lets Third Planet *lead* another program with that harmony. Over the MPC's USB MIDI
connection, Third Planet sends:

- the chord playing now, and the next chord and how many bars away it is;
- the progression's slots and the key and mode;
- the form's section and the bar of the phrase;
- how much each layer (Drone, Seq, Lead, Seq 2, Atmosphere) is playing;
- the player's actions (Next chord, Hold, a mute, a piece change) as they happen.

The other program can send two commands back: a **fade** of Third Planet's output over a number of bars, and
**Hold**.

Everything travels as open MIDI SysEx under the MIDI non-commercial manufacturer ID. It needs no extra MPC
track, MIDI routing or network: the computer sees the MPC as a USB MIDI device ("MPC Live III Port 1").

## Why it exists

It was built so a generative sequencer on a computer could play along with Third Planet: Third Planet decides
the harmony and the form, and the sequencer's own parts follow its chords and fade with it. The format is
documented and open so that anyone can write the other side for their own software.

## Building with Harmony Link

From the repository root:

```bash
plugins/ThirdPlanet/tools/build_variant.sh on
```

This builds the plugin with `TP_LINK=1`, regenerates the skin so the PLAY page gets a **Link** switch, and
builds again so the plugin folder carries that skin. It rewrites the committed `plugins/ThirdPlanet/skins/`; run
`plugins/ThirdPlanet/tools/build_variant.sh off` to go back to the default skin before committing.

(With the SDK directly: `python3 deps/mpc-plugin-sdk/tools/build.py plugins/ThirdPlanet --define TP_LINK=1` —
but then the skin is the default one, without the Link switch.)

## Using it

1. Connect the MPC to the computer by USB.
2. Turn on **Link** on the PLAY page. It is Off by default, and with Link Off the plugin behaves exactly like the
   build without Harmony Link.
3. While Link is on, Third Planet sends a PLAN whenever the harmony changes (while only the layers' activity
   changes, twice a second) and again every 2 seconds, each action at once as an EVENT, and BYE when Link is
   turned off. A FADE runs over the host's bars; HOLD acts like the Hold switch. The MPC's Play brings the level
   back to full and releases a Hold that a command set; loading a project brings the level back to full too.

## The format and tools

- The wire format: [`plugins/ThirdPlanet/docs/harmony-link.md`](../plugins/ThirdPlanet/docs/harmony-link.md).
- Golden vectors to test an implementation against:
  [`plugins/ThirdPlanet/docs/harmony-link-vectors.txt`](../plugins/ThirdPlanet/docs/harmony-link-vectors.txt).
- Third Planet's codec: `plugins/ThirdPlanet/src/link_codec.{h,cpp}`.
- Two small macOS tools in `plugins/ThirdPlanet/tools/` (build with
  `swiftc -O plugins/ThirdPlanet/tools/midirecv.swift -o midirecv`, likewise `midisend`):

| Command | What it does |
| --- | --- |
| `midirecv "MPC Live III Port 1" --link` | Prints each Link message on one line, for example `PLAN #33 key E Lydian \| now Emaj7#11 → next Am9 held \| sec Bridge bar 7/8 \| layers D:90 S:0 L:45 S2:127 A:13 \| timed pos 13`. `--full` adds the PLAN's other fields. |
| `midirecv --decode-file plugins/ThirdPlanet/docs/harmony-link-vectors.txt` | Decodes every vector and exits nonzero if one fails. |
| `midisend "MPC Live III Port 1" fade:0:16` | FADE down to silence over 16 bars (`fade:LEVEL:BARS`, level 0..127, bars 0..16383). |
| `midisend "MPC Live III Port 1" hold:1` | HOLD on (`hold:0` is off). |
| `midisend PORT sx:F07D545001110001F7` | Any raw SysEx, from F0 to F7. |

To try the tools without an MPC, use the macOS IAC bus: `midirecv IAC --link` in one terminal and
`midisend IAC fade:0:16 hold:1` in another.

## Status

Harmony Link was tested on an MPC Live III with MPC OS 3.10.1 and with its own test suite (see
[CONTRIBUTING.md](../CONTRIBUTING.md)). It is provided as is, for anyone who wants to continue with it.
