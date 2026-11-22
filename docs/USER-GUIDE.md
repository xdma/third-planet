# Third Planet user guide

Third Planet is a generative 1970s Berlin School instrument for Akai MPC standalone (MPC OS 3.10.1 or later).
It is a port of **NACHTFAHRT** by Cranky Man, a firmware for the M-VAVE FM-1 synthesizer. Third Planet runs
NACHTFAHRT's own sound engine and app code, so it sounds and behaves like NACHTFAHRT on an FM-1. You do not
need to know the FM-1 to use it: this guide describes everything in MPC terms.

Installation is covered in the [README](../README.md).

## Contents

1. [What it does by itself](#1-what-it-does-by-itself)
2. [Setting up the pads](#2-setting-up-the-pads)
3. [The screen](#3-the-screen)
4. [Control reference, page by page](#4-control-reference-page-by-page)
5. [Sync, saving and limits](#5-sync-saving-and-limits)

---

## 1. What it does by itself

Load Third Planet on a track and it starts playing at once. You do not have to press anything. It plays five
layers:

- **Sequence**: a short plucked pattern of up to 16 steps in the piece's scale. Each note is played again by
  its **repeats** (real notes at an echo time), which gives the classic Berlin School pulse.
- **Sequence 2**: a second pattern, by default an octave lower and one step shorter. The two patterns drift
  apart and meet again.
- **Drone**: a sustained chord with a pedal note an octave below the root. The chord walks from degree to
  degree every few bars, or follows chords you write.
- **Lead**: one gliding voice that plays phrases in pairs, a call and an answer. It remembers its motifs and
  varies them. Play three or more notes on the Lead pads and pause: the Lead takes your notes as its theme,
  answers you and grows new ideas from them.
- **Atmosphere**: a background texture such as wind, surf, rain or tape crackle.

Everything goes through a tape-style echo and a plate reverb.

Nothing fades behind your back. What moves is each layer's **filter**: every layer's brightness wanders slowly
around the value you set. A piece also moves through **sections**: Emerge (the opening, while filters open),
Flow, Solo (the Lead in full) and Bridge (the layers' filters part and come back together). A piece never ends
by itself.

**Evolve** makes slow changes over time: the sequences change a step or a note now and then, and above a set
level the instrument nudges a layer's sound by itself.

There are ten **pieces**. Each one sets its own key, tempo, voices, patterns and movement:

| Piece | Key | Tempo |
| --- | --- | --- |
| Timewind | D minor | 96 |
| Wahnfried | B♭ Dorian | 80 |
| Phaedra | D minor | 149 |
| Rubycon | F minor | 144 |
| Stratosfear | E minor | 138 |
| Sunrain | D major | 130 |
| Mirage | F♯ minor | 95 |
| Cyborg | C Phrygian | 70 |
| Sakura Rain | B♭ Dorian | 80 |
| Umeda | F♯ major pentatonic | 50 |

Each piece remembers your changes. When you switch to another piece and back, it comes back as you left it.
**Reset piece** (PLAY page) puts it back to its original settings.

---

## 2. Setting up the pads

The pads and any MIDI keyboard only play. Everything that switches something on or off, or acts once, is on
the touch screen.

### Pad Perform settings

On the track that holds Third Planet, set the MPC's **Pad Perform**:

- Pad Perform **on**, mode **Chromatic** (not Notes), root **C**, scale **Chromatic**.
- Octave set so that bank A pad 1 is **C1, note 36**.
- Banks: the **continuous** option, not "Start on root note". "Start on root note" makes the banks overlap
  (A 36–51, B 48–63, C 60–75), and then the banks below do not line up.

Bank A is then notes 36–51, bank B 52–67 and bank C 68–83.

### What the banks do

Each pad acts **while it is held** and stops when you let go. Velocity is ignored.

| Bank | Notes | Role |
| --- | --- | --- |
| A | 36–51 | **Lead**: pad 1 to 16 play scale degrees 1 to 16 of the piece |
| B | 52–67 | **Effects**: 16 performance effects |
| C, pads 1–8 | 68–75 | **Moves**: 8 performance moves |
| C, pads 9–16, and any other note | 76–83, and outside 36–75 | The Lead, moved to the nearest note of the scale |

These note numbers assume **Pad base** is 36 (section 4, PLAY page).

#### Bank A: the Lead

Pad 1 is the piece's root, pad 2 the next note of the scale, and so on up through 16 scale steps. **Lead
octave** (LEAD page) shifts them. The Lead is monophonic: the last pad held sounds, and a new pad pressed
while another is held plays legato (it glides).

Play a figure of three or more notes and pause for about two beats: the Lead answers it and takes it as the
piece's theme. One or two notes are just played. **Forget** (PLAY page) makes the Lead drop its theme and ideas
and start fresh.

#### Bank B: the effects

All effects can be held together.

| Pad | Note | Effect | What you hear |
| --- | --- | --- | --- |
| 1 | 52 | Repeat | The Sequence thrown hard into the echo. |
| 2 | 53 | Time | The echo time doubled. |
| 3 | 54 | Tape stop | The echo's tape slows down, to a quarter of its speed. |
| 4 | 55 | Shimmer | The plate's tail is pitched up an octave and fed back, so it climbs in octaves. |
| 5 | 56 | Low-pass | A low-pass filter on the whole mix, down to about 350 Hz. |
| 6 | 57 | High-pass | A high-pass filter on the whole mix, up to about 1.3 kHz. |
| 7 | 58 | Sweep | The Sequence's filter swept two octaves up and down, about one sweep every 3 seconds. |
| 8 | 59 | Phaser | A deep phaser on the whole mix. |
| 9 | 60 | Reverb | The dry music sent into the plate reverb. |
| 10 | 61 | Take-off | A burst of filtered noise, falling into the echo. Fires once on the press. |
| 11 | 62 | Freeze | The granular "cloud" holds what it has and keeps repeating it. |
| 12 | 63 | Wash | Everything into the echo and the plate, with the dry sound turned down. |
| 13 | 64 | Octave | The Sequence plays an octave up. |
| 14 | 65 | Return | Both sequences loop round their first four steps. |
| 15 | 66 | Twitter | A stream of bleeps into the echo. |
| 16 | 67 | Supernova | The echo's feedback pushed past full, so it builds up. |

Freeze is not the same as **Hold** on the PLAY page.

#### Bank C: the moves

| Pad | Note | Move | What you hear |
| --- | --- | --- | --- |
| 1 | 68 | Rise | Every layer's filter opens together (up to 2 octaves), then eases back when you let go. |
| 2 | 69 | Close | Every layer's filter closes together (down 4 octaves): a breakdown by hand. |
| 3 | 70 | Transpose | Both sequences move three scale steps up. The Drone's chord and the Lead stay. |
| 4 | 71 | Sweep | The Sequence's filter opened two octaves. |
| 5 | 72 | Open | A resonant high-pass on the mix sweeps up: thin and bright. |
| 6 | 73 | Shut | A low-pass on the mix sweeps down: dark and muffled. |
| 7 | 74 | Drop | Both sequences drop out. |
| 8 | 75 | Echo | Everything into the echo, with more feedback. |

Open and Shut push the same filter as the **Filter** knob (SKY page), on top of where the knob is.

#### Other notes

Notes outside the three banks, and every note when **Keys** is set to Lead, play the Lead. Each note sounds as
the nearest note of the piece's scale, and Lead octave shifts it. A figure played this way also becomes the
Lead's theme.

---

## 3. The screen

The plugin has eleven pages (tabs), in this order:

| Page | What is on it |
| --- | --- |
| SKY | The star chart (what is playing now) and the 16 main controls |
| PLAY | Hold, Lock, Play, the one-shots, Keys, Pad base, Sync |
| LAYERS | Mute, Solo, Nudge and Shake for each layer |
| DRONE | The Drone's sound, movement and voice |
| SEQ | The Sequence's pattern, sound, repeats, voice and movement |
| LEAD | The Lead's playing, sound, voice and movement |
| SEQ 2 | Sequence 2's pattern, sound, voice and movement |
| PATTERN | Write the sequences' steps and accents by hand |
| ATMOS | The Atmosphere's type, level, sound and movement |
| SPACE | The echo and the plate; the arc; Transpose and Tune |
| CHORDS | The eight chord slots and how they play |

### The status line

Every page except SKY has a status line across the top:

- **Piece**: the piece playing.
- **Chord**: the chord sounding now.
- **Pad**: what the last pad or note did, for example "Lead 3", "Shimmer", "Rise" or "Note 90". If another
  Third Planet is already playing, it says so here (section 5).
- **HOLD** and **LOCK**, while they are on.

### The SKY page

SKY is the first page. It shows the piece as a star chart:

- **Header**: the piece, the tempo ("EXT" while Sync follows the MPC), the key and mode, and HOLD, LOCK and
  SOLO while they are on.
- **Centre**: the chord sounding, on a disc. Particles circle it: an inner orbit for each Sequence note and an
  outer orbit for each Sequence 2 note. A ring of dots around it fills up as the chord nears its change, and
  looks different in the chord's last bar and while Hold is on.
- **Constellation**: eight stars for the eight chord slots (CHORDS page), each with its chord name. A star
  shows whether its slot is a gap, written by the walk, written by you, or playing now.
- **Left column**: ARC is the current section (EMERGE, FLOW, SOLO, BRIDGE). PHRASE shows the bar of the 8-bar
  phrase. Under it, the chance that Evolve nudges a layer this phrase. NEXT is the next chord and when it comes
  ("ON THE BAR", "IN 3 BARS", or "ON HOLD"), then the progression mode ("ENDLESS · HOME", "LOOP 3/8"). LOG lists
  the last three changes and who made them: "You" (your one-shots and switches), "Evolve" (its automatic
  nudges) or "Piece" (a piece change), for example "You › Mute Seq". The newest line is highlighted for a
  moment.
- **Right column**: five planets, one per layer (DRONE, SEQ, LEAD, SEQ 2, ATMOS). Each planet shows how much
  its layer is playing and whether it is muted or soloed out, and it flashes when the layer is nudged.
- **Bottom strip**: 16 controls in four groups of four (section 4). The two sequence levels are labelled SEQ
  and SEQ 2 there.

On this page the Q-Links play the bottom strip: Q-Links 13–16 are Filter, Evolve, Motion and Drift; 9–12 are
Energy, Wet, Sweep and Volume; 5–8 are the four levels; 1–4 are Piece, Tempo, Root and Mode. Tap a value in
the strip to give its group the Q-Link focus; double-tap it to open its value. On the other knob pages the
Q-Links follow the controls on screen. One-shots are never on a Q-Link.

---

## 4. Control reference, page by page

### How to read the tables

- **Values** gives the range or the list of choices.
- **Default** is the value on a fresh load. A fresh load starts the first piece, Timewind, so most defaults are
  Timewind's settings. Each piece sets its own values when it loads, and the knobs move to show them. Values
  marked *(global)* belong to the plugin and do not change with the piece.
- Values that the instrument changes by itself (a new piece, Evolve's nudges, a shake, the Filter, Wet and
  Energy movement) show up on the MPC's knobs.

**Switches and one-shots.** Some screen buttons are **switches**: they show a state and a tap sets it (Hold,
Lock, Play, Sync, the mutes and solos, the pattern steps and accents). Others are **one-shots**: a tap acts
once and the button goes back to Off by itself (Forget, Next chord, New chords, Shake all, Reset piece, the
nudges and shakes).

**The Move controls.** Each layer has the same four controls for its filter movement (Width, Time, Rate,
Intro). They work the same way on every layer:

| Control | What it does |
| --- | --- |
| Width | How far the layer's filter may wander either way from the value you set. 0: no wandering. The Lead's movement is kept gentle. For the Sequence this control is **Sweep** on the SKY page. |
| Time | How long one filter move takes to arrive. |
| Rate | How often a new move starts. 0: never (the filter stays where its last move left it). |
| Intro | 0–64 bars. When the piece starts, the filter begins closed and opens over this many bars. 0: open from the start. |

The movement follows the bars, so it stands still while the music rests.

### SKY (bottom strip)

| Control | Values | Default | What it does |
| --- | --- | --- | --- |
| Filter | Shut 100 … Off … Open 100 | Off | A filter on the whole mix. Turned down it shuts (a low-pass); turned up it opens (a resonant high-pass). With Motion it also moves by itself (see below). Not stored with the piece. |
| Evolve | 0–100 | 40 | How often the sequences change a step, a note or an octave. Above 40, at the start of each 8-bar phrase there is a chance that one layer is nudged by itself: 20% at 70, 40% at 100. These nudges change the saved values. |
| Motion | 0–100 | 45 | How much the sequences change and how quickly the sections move on. At 0 the piece stays in Flow. It also sets how far Density drifts, how far Filter, Wet and Energy move by themselves, and how often Sequence 2 shifts an octave at a section change. |
| Drift | 2, 4, 8, 16, 32, 64 bars, Hold | 16 bars | How many bars each chord lasts before the walk moves on. Hold: the chord does not change by itself. |
| Energy | −50 … +50 | +0 | Sparser or busier: moves the sequences' Density, levels, Sweep, note length, Cutoff and Repeat B together. Moves by itself with Motion, following the arc. Not stored with the piece. |
| Wet | −100 … +100 | +0 | Drier or wetter: moves every layer's echo send and the main Echo together. Moves by itself with Motion. Not stored with the piece. |
| Sweep | 0–100 | 50 | How far the Sequence's filter wanders by itself (the Sequence's Move width). |
| Volume | 0–200% | 100% *(global)* | The output level. 100% is unity. |
| Drone | 0–100 | 60 | The Drone's level. |
| Sequence | 0–100 | 70 | The Sequence's level. |
| Lead | 0–100 | 40 | The Lead's level. |
| Sequence 2 | 0–100 | 60 | Sequence 2's level. A sequence at level 0 stays silent whatever Energy does. |
| Piece | Timewind, Wahnfried, Phaedra, Rubycon, Stratosfear, Sunrain, Mirage, Cyborg, Sakura Rain, Umeda | Timewind | The piece. The piece you leave is kept as it is; the new one comes back as you last left it. Mutes, solos, Hold and Lock stay as they are. |
| Tempo | 60–160 BPM | 96 | The piece's tempo. Ignored while Sync follows the MPC. |
| Root | C … B | D | The key's root. The chord is re-voiced at once. |
| Mode | Minor, Major, Dorian, Mixolyd., Pent min, Pent maj, Phrygian, Lydian | Minor | The scale. Everything (chords, sequences, Lead) follows it. |

**Filter, Wet and Energy move by themselves.** With Motion above 0, these three swing slowly on their own and
their knobs show it. When you turn one, your value holds until the next section change; then the movement takes
over again from your value, without a jump. **Lock** holds all three where they are. A piece change or **Reset
piece** returns them to 0.

### PLAY

| Control | Values | Default | What it does |
| --- | --- | --- | --- |
| Hold | Off / On (switch) | Off | The echo and the plate stop taking in new sound and keep repeating what they hold, while the music plays on dry above them. Chord changes wait, and Evolve's automatic nudges pause. Not the Freeze effect. |
| Lock | Off / On (switch) | Off | The sequences stop varying: each pass plays as the last. Evolve's automatic nudges stop, and Filter, Wet and Energy hold where they are. |
| Play | Off / On (switch) | On | On: the piece plays from the top, with fresh randomness. Off: it rests, and the echo and reverb ring out. While Sync is on, the MPC's transport starts and stops it. |
| Forget | one-shot | | The Lead forgets its ideas and its theme (yours too) and starts again with new ones within a few bars. No setting changes. |
| Next chord | one-shot | | The chord moves on: on the next bar while playing, at once while resting. |
| New chords | one-shot | | The chord walk writes a new progression in its slots. The chord playing, your written chords and the gaps are kept (CHORDS page). |
| Shake all | one-shot | | Shakes the Drone, Sequence, Lead, Sequence 2 and Atmosphere at once (see LAYERS). The Space is not shaken. |
| Reset piece | one-shot | | Puts the current piece back to its original settings and grows its patterns afresh. Your changes to this piece are lost. |
| Keys | Pads, Lead | Pads *(global)* | Pads: notes from Pad base play the three pad banks (section 2). Lead: every note plays the Lead. A pad held while this changes is let go. |
| Pad base | 0–88 | 36 *(global)* | The first note of the pad banks. The banks take the 40 notes from here up. Change it for another pad layout or a controller. A pad held while this changes is let go. |
| Sync | Off / On (switch) | Off *(global)* | Off: the piece keeps its own tempo and plays all the time. On: it follows the MPC's tempo and transport (section 5). |

The PLAY page also has a **Link** slot. Link exists only in builds made with Harmony Link enabled; in the
standard build that slot is empty. See [HARMONY-LINK.md](HARMONY-LINK.md).

### LAYERS

One column per layer: DRONE, SEQ, LEAD, SEQ 2, ATMOS, and SPACE (the echo and plate). Each column has the
buttons Mute, Solo, Nudge and Shake under its title; SPACE has only Nudge and Shake. The table names them as
the MPC's parameter list does: "Mute Drone" is the Mute button in the DRONE column.

| Control | Values | What it does |
| --- | --- | --- |
| Mute Drone, Mute Seq, Mute Lead, Mute Seq 2, Mute Atmos | Off / On (switch) | Silences the layer. Its level is kept. |
| Solo Drone, Solo Seq, Solo Lead, Solo Seq 2, Solo Atmos | Off / On (switch) | Plays only the soloed layers. More than one can be soloed. A layer that is soloed and muted stays muted. |
| Nudge Drone, Nudge Seq, Nudge Lead, Nudge Seq 2, Nudge Space, Nudge Atmos | one-shot | Up to four of the layer's sound values take a small random step (list below). Each step tends back toward the piece's original value, so nudges explore around the piece's sound. |
| Shake Drone, Shake Seq, Shake Lead, Shake Seq 2, Shake Space, Shake Atmos | one-shot | A bigger change: a new voice (Space: a new Echo time; Atmos: a new type) and bigger steps on all of the layer's values below. |

Defaults: every switch Off. Mutes and solos are kept across piece changes.

What a nudge or shake can change:

| Layer | Values | Shake also picks |
| --- | --- | --- |
| Drone | Drone attack, Drone colour, Drone reso, Drone release, Phaser, Pedal | a Drone voice |
| Seq | Seq cutoff, Seq decay, Accent | a voice from 2600 to Pluck |
| Lead | Lead tone, Lead attack, Vibrato, Glide | a voice from Odyssey to Synthi (never Choir) |
| Seq 2 | Seq2 cutoff, Seq2 decay | a voice from 2600 to Pluck |
| Space | Echo, Feedback, Reverb, Plate size, and the cloud's density (no control of its own) | an Echo time |
| Atmos | Atmos colour, Atmos density, Atmos ring, Atmos air | an Atmos type |

Shakes never pick the FM voices, the Piano or the Lead's Choir; choose those with the voice controls.

### DRONE

| Control | Values | Default | What it does |
| --- | --- | --- | --- |
| Pedal | 0–100 | 70 | The level of the root an octave below the chord. |
| Drone reso | −50 … +50 | 0 | The resonance of the Drone's filter. On Choir it makes the formants sharper or softer. 0 is the voice as designed. |
| Phaser | 0–100 | 60 | The depth of the slow phaser on the Drone. |
| Drone attack | 0–100 | 40 | How slowly the chord swells in: about 0.5 to 12 seconds. |
| Drone colour | −50 … +50 | 0 | Darker or brighter: the Drone's filter, up to two octaves either way. On Choir it picks the vowel. |
| Drone release | 0–100 | 50 | How long the chord takes to die away: about 1 to 15 seconds. |
| Drone reverb | 0–100 | 50 | The Drone's send to the plate reverb. 50 is normal, 100 double. |
| Drone echo | 0–100 | 50 | The Drone's send to the echo. 50 is normal, 100 double. |
| Drone width | 0–100 | 30 | Move width (see "The Move controls"). |
| Drone time | 0–100 | 50 | Move time. |
| Drone rate | 0–100 | 25 | Move rate. |
| Drone intro | 0–64 bars | 0 | Move intro. |
| Drone voice | Organ, Strings, Horn, Saws, Glass, Choir | Organ | The Drone's voice. |

### SEQ

| Control | Values | Default | What it does |
| --- | --- | --- | --- |
| Density | 0–100 | 65 | How many of the steps sound; the least important drop out first. With Motion it also drifts slowly denser and sparser by itself. |
| Seq octave | −2 … +2 | 0 | The Sequence's octave. |
| Seq length | 3–16 steps | 16 | The pattern's length. |
| Seq grid | 8ths, 16ths | 16ths | The step size. |
| Seq cutoff | 0–100 | 45 | The brightness of the plucks: the filter from about 110 Hz to 5.4 kHz. The filter movement wanders around this value. |
| Seq decay | 0–100 | 35 | How long each note lasts. |
| Seq reverb | 0–100 | 50 | The Sequence's send to the plate. 50 is normal, 100 double. |
| Seq echo | 0–100 | 50 | The Sequence's send to the echo. 50 is normal, 100 double. |
| Repeat A | 0–100 | 100 | The level of the first repeat. Each note is played again after A time; 100 is as loud as the note. |
| A time | 1/8, 1/8., 1/4, 1/4., 1/2, 1 bar | 1/8. | When the first repeat comes. |
| Repeat B | 0–100 | 50 | The level of a second repeat, which plays only into rests. |
| B time | 1/8, 1/8., 1/4, 1/4., 1/2, 1 bar | 1/4 | When the second repeat comes. |
| Seq voice | 2600, Moog, Synthi, Pulse, Glass, Pluck, FM Keys, FM Bell, FM EP, FM Clav, FM Guitar, FMMarimba, FM Vibes, FM Metal, FM Bass, FM Sub, Piano | 2600 | The Sequence's voice. |
| Seq time | 0–100 | 50 | Move time. |
| Seq rate | 0–100 | 30 | Move rate. |
| Seq intro | 0–64 bars | 32 | Move intro. |

The Sequence's Move width is **Sweep** on the SKY page.

### LEAD

| Control | Values | Default | What it does |
| --- | --- | --- | --- |
| Phrases | 0–100 | 50 | How often the Lead plays: higher means shorter breaths between its phrases. |
| Lead octave | −2 … +1 | 0 | The Lead's octave. Also shifts the Lead pads and notes. |
| Glide | 0–100 | 25 | The glide time between notes, about 1 to 150 ms. |
| Vibrato | 0–100 | 40 | The vibrato depth. |
| Lead tone | 0–100 | 50 | Darker or brighter: the Lead's filter, up to two octaves either way. On Choir it picks the vowel. |
| Lead attack | 0–100 | 10 | How slowly each note starts: about 5 to 400 ms. |
| Lead reverb | 0–100 | 90 | The Lead's send to the plate. 50 is normal, 100 double. |
| Lead echo | 0–100 | 90 | The Lead's send to the echo. 50 is normal, 100 double. |
| Lead width | 0–100 | 15 | Move width. |
| Lead time | 0–100 | 50 | Move time. |
| Lead rate | 0–100 | 30 | Move rate. |
| Lead intro | 0–64 bars | 0 | Move intro. |
| Lead voice | Odyssey, Mini, Flute, Theremin, Synthi, FM Lead, FM Reed, FM Brass, Choir, Piano | Odyssey | The Lead's voice. |

When the Lead starts playing in a piece, and how it keeps its ideas, are set by **Lead in** and **Memory** on
the SPACE page.

### SEQ 2

| Control | Values | Default | What it does |
| --- | --- | --- | --- |
| Seq2 chance | 0–100 | 100 | The chance that each of Sequence 2's steps plays. Lower thins it at random. |
| Seq2 oct | −2 … +1 | −1 | Sequence 2's octave. With Motion, Sequence 2 may move one octave away at a section change and back at the next; the control shows where it is. Setting it makes your value the octave again. |
| Seq2 length | 3–16 steps | 15 | The pattern's length. A length different from the Sequence's makes the two drift apart and meet again. |
| Seq2 voice | 2600, Moog, Synthi, Pulse, Glass, Sub, Pluck, FM Keys, FM Bell, FM EP, FM Clav, FM Guitar, FMMarimba, FM Vibes, FM Metal, FM Bass, FM Sub, Piano | Moog | Sequence 2's voice. |
| Seq2 cutoff | 0–100 | 40 | The brightness of the plucks, as Seq cutoff. |
| Seq2 decay | 0–100 | 35 | How long each note lasts. |
| Seq2 reverb | 0–100 | 50 | Sequence 2's send to the plate. 50 is normal, 100 double. |
| Seq2 echo | 0–100 | 50 | Sequence 2's send to the echo. 50 is normal, 100 double. |
| Seq2 width | 0–100 | 20 | Move width. |
| Seq2 time | 0–100 | 50 | Move time. |
| Seq2 rate | 0–100 | 30 | Move rate. |
| Seq2 intro | 0–64 bars | 0 | Move intro. |

Sequence 2's level is **Sequence 2** on the SKY page.

### PATTERN

The PATTERN page lets you write the two sequences by hand. It has four controls and, below them, 16 step
buttons and 16 accent buttons.

| Control | Values | Default | What it does |
| --- | --- | --- | --- |
| Lane | Steps, Steps 2, Octaves, Octaves 2, Skips | Steps | Which lane the step and accent buttons show. Steps: which of the Sequence's steps play. Steps 2: which of Sequence 2's steps play. Octaves, Octaves 2: which steps jump an octave (Sequence, Sequence 2). Skips: which of the Sequence's steps are passed over. |
| Accent | 0–100 | 50 | How much quieter the unaccented steps are. 0: all steps equal. Applies to both sequences. |
| Lane chance | 0–100 | 100 | The chance that each step of the lane's sequence plays. On Steps, Octaves and Skips it is the Sequence's chance; on Steps 2 and Octaves 2 it is the same value as Seq2 chance. |
| Pattern | Auto, Pattern | Auto | Auto: the sequences are generated. Pattern: they play the lanes as written. |
| Step 1 … Step 16 | Off / On (switch) | | The lane's 16 steps. In Auto they show what is playing now. |
| Accent 1 … Accent 16 | Off / On (switch) | | Accents on those steps. The Steps, Octaves and Skips lanes use the Sequence's accents; Steps 2 and Octaves 2 use Sequence 2's. Turning an accent on also turns its step on in the lane shown. |

Changing a step or an accent while Pattern is on Auto first copies what Auto is playing into the lanes, then
switches Pattern to Pattern. You start from the current pattern, not from blank. In Pattern mode, Density does
not thin the steps, and Evolve and Motion do not vary them; the chance controls still apply.

### ATMOS

| Control | Values | Default | What it does |
| --- | --- | --- | --- |
| Atmos density | −50 … +50 | 0 | How busy the texture is. 0 is the type as designed. |
| Atmos spread | 0–100 | 50 | The stereo spread of the texture. |
| Atmos drift | −50 … +50 | 0 | How quickly the texture changes. 0 is the type as designed. |
| Atmos air | −50 … +50 | 0 | The level of the soft hiss under the texture. 0 is the type as designed. |
| Atmos colour | −50 … +50 | 0 | Darker or brighter. 0 is the type as designed. |
| Atmos ring | 0–100 | 50 | How much the texture rings (how resonant or how long its sounds are). |
| Atmos reverb | 0–100 | 50 | The Atmosphere's send to the plate. |
| Atmos echo | 0–100 | 50 | The Atmosphere's send to the echo. |
| Atmos width | 0–100 | 30 | Move width (the filter movement, not the stereo width). |
| Atmos time | 0–100 | 50 | Move time. |
| Atmos rate | 0–100 | 30 | Move rate. |
| Atmos intro | 0–64 bars | 0 | Move intro. |
| Atmos type | None, Wind, Surf, Space, Air, Tape, Drops, Hail, Stereo Ra(in), Water | Wind | The texture. Stereo Rain is shown as "Stereo Ra". |
| Atmosphere | 0–100 | 7 | The Atmosphere's level. |

Atmos drift is not the same as **Drift** on the SKY page, and Atmos air is not the Air type.

### SPACE

| Control | Values | Default | What it does |
| --- | --- | --- | --- |
| Echo | 0–100 | 55 | How much of the whole mix goes into the echo. The echo is a tape echo that bounces left and right. |
| Echo time | 1/8, 1/8., 1/4, 1/4., 1/2, 1 bar | 1/4. | The echo time, in beats of the tempo. |
| Feedback | 1.5 s … 15 s | 6.0 s | How long the echo's tail rings, shown as its length. |
| Reverb | 0–100 | 50 | How much of the whole mix goes into the plate reverb. |
| Echo blur | 0–100 | 50 | How smeared the repeats are: low gives clean copies, high blurs them. |
| Echo tone | 0–100 | 50 | The brightness of what enters the echo: a low-pass at about 2 kHz at 50. |
| Echo width | 0–100 | 50 | The echo's stereo spread: 0 mono, 100 wide. |
| Plate size | 0–100 | 55 | The plate reverb's size: higher is a longer tail. |
| Lead in | 0–64 bars, Off | 32 | How many bars after the piece starts the Lead may begin its own phrases. Off: the Lead plays only what you play on the pads or MIDI, with no phrases or answers of its own. |
| Bridge | 0–100 | 45 | How far each layer's filter moves away during a Bridge section, before the layers come back together. 0: a Bridge is like Flow. |
| Arc | 0–100 | 25 | The length of the Sequence's slow rise: its filter climbs toward a peak and settles, again and again. 0: no rise. 1–100: about 130 to 770 bars. Energy follows this rise. |
| Memory | 0–100 | 55 | How much the Lead keeps its ideas. High: it holds on to a motif. Low: it changes it nearly every phrase. |
| Transpose | Parallel, Diatonic, Bed | Parallel | How the sequences follow the chord. Parallel moves the whole pattern by the chord's interval (it may leave the scale). Diatonic moves it by scale steps (it stays in the scale). Bed does not move it. |
| Tune | −50 … +50 cents | 0 *(global)* | Fine tuning of the whole instrument. |

### CHORDS

The Drone's chords come from eight **slots**. Each slot is one of three things:

- **Written by the walk**: a chord the walk chose, as a scale degree. It follows the key and mode.
- **Written by you**: a chord with a fixed root, kind and type.
- **A gap** (shown as "-"): the chord before it holds.

In **Endless** mode, slot 1 plays. At each change the slots move up one place and the walk writes a new chord
at the end. In **Loop** mode, the slots play in turn, up to the last slot that is not a gap, and nothing new is
written.

| Control | Values | Default | What it does |
| --- | --- | --- | --- |
| Chord slot | 1–8 | 1 | Which slot the three controls below show and edit. |
| Chord root | -, C, C#, D, D#, E, F, F#, G, G#, A, A#, B | (the slot's) | The slot's root. "-" makes the slot a gap. On a walk slot, setting the root turns it into a written chord. |
| Chord kind | Empty, Major, Minor, Sus2, Sus4, Dim, Aug | (the slot's) | The chord's kind. Empty makes the slot a gap. On a walk slot, it turns the slot into a written chord. |
| Chord type | Triad, 6, 7, Maj7, 9, 11, Add9 | (the slot's) | The chord's extension. On a gap it has no effect until the slot gets a kind. |
| Chord play | Endless, Loop | Endless | How the slots play (see above). |
| Walk | Home, Wander, Free | Home | How the walk chooses chords. Home returns to the home chord most often and leans on the IV, VII and V. Wander roams wider. Free is even. Changing it rewrites the walk's slots at once. |

To write a chord on an empty slot, set **Chord kind** first, then **Chord root**. The playing slot changes at
once when you edit it.

---

## 5. Sync, saving and limits

### Sync

- **Off** (the default): the piece keeps its own Tempo and plays all the time, whatever the MPC's transport
  does.
- **On**: Third Planet follows the MPC's tempo and transport. The MPC's Play starts the piece from the top and
  Stop rests it. If you start the MPC in the middle of a bar, the piece waits for the next bar line, so the
  piece's bar 1 lines up with a bar of your sequence. While Sync is on, the tempo shows "EXT".
- Turning Sync off while the MPC is stopped starts the piece again on its own tempo.

### Saving with the project

The MPC project keeps Third Planet's full state: every piece as you left it, plus the knob values. When the
project loads, Third Planet starts as from power-on with that state, then applies the knob values.

The project also restores Hold, Lock, Play, the mutes and solos, Filter, Wet, Energy, the Pattern lane and the
chord slot shown. One-shots never fire on a load.

### One instance at a time

The engine exists once per MPC. A second Third Planet stays silent, and its status line says "Another Third
Planet plays", until the first is removed. Then it takes over with its own settings.

### What the FM-1 has that the MPC version does not

- The FM-1's own screen (its pictures and help cards). The status line and the SKY page show the main facts
  instead.
- Its pages and PERFORM as modes, and its button gestures (taps, holds and combinations). On the MPC every
  gesture is a screen control or a pad.
- MIDI out. The MPC does not pass MIDI out of a plugin.
- The button and key lights, and the Lights setting.
- The FM-1's own save gesture and Save knob. The MPC project saves instead.
- The knobs' centre notch and acceleration, because the MPC's knobs are absolute.
