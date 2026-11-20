// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: the plugin's handle on the FM-1 app running in app_unit.cpp (NACHTFAHRT's ui.c and
// project.c on a virtual panel). Everything here runs on the audio thread, as the engine does.
#pragma once

#include <cstdint>

namespace tp {
namespace app {

// The FM-1's panel, by label (plat.h B_* and EN_*).
enum Button { kFX, kSEL, kENV, kLFO, kEDIT, kGLO, kHOME, kSAVE, kARP, kSEQ, kPLAY, kREC, kOCTDN, kOCTUP, kNumButtons };
enum Encoder { kSELECT, kALGO, kPRESET, kK1, kK2, kK3, kK4, kNumEncoders };
constexpr int kNumKeys = 27;   // F3 .. G5; key 0 = F3

// The device from power-on: the engine reset, the project loaded from the store (or the recipes when
// the store is empty), everything sent to the engine, the UI started. Time starts at 0.
void boot();

// One simulated millisecond, as upstream's host simulator runs it: a 256-frame block rendered into
// `chunk` if one has fallen due (returns true), then the UI (a frame every 16 ms, input in between).
bool stepMs(int32_t* chunk);

// The virtual panel. Held buttons and keys are bit masks (bit = Button / key index).
void setButtons(uint32_t mask);
void setKeys(uint32_t mask);
void turn(int encoder, int detents);
void setAccelOff(bool off);          // exact detents, no acceleration (the simulator's "spin")
void setMaster(uint32_t q12);        // the MASTER pot as gain Q12 (2048 = unity, as upstream's default)
void midiIn(uint32_t usbPacket);     // a USB-MIDI event packet: CIN | status << 8 | d1 << 16 | d2 << 24
uint32_t litButtons();
uint32_t litKeys();

// The saved project (one flash object). storeGen() moves on whenever the app saves.
constexpr int kStoreMax = 3840;
uint32_t storeGen();
int storeRead(uint8_t* dst, int max);   // bytes, < 0: nothing saved
void storeWrite(const uint8_t* src, int len);   // len < 0: empty (the recipes on the next boot)

// The app's knobs: a parameter (P_*, 0..130) or one of ui.c's own knob codes.
enum KnobCode
{
    kKnobLeds = 200, kKnobCpu, kKnobNone, kKnobSlot, kKnobChord, kKnobType, kKnobLane, kKnobLaneChance,
    kKnobFilter, kKnobWet, kKnobEnergy, kKnobSpace, kKnobMode, kKnobLoop, kKnobSave
};
void knob(int code, int value);   // as the knob writes it (knob_set), counted as a turn
int knobValue(int code);          // as the knob shows it (knob_get)
void voice(int param, int value); // a layer's voice (ALGO)
void walk(int character);         // the Chords page's walk character (ALGO there)
int walkValue();
void setPiece(int piece);         // PRESET
int par(int param);
int piece();
int modeAt(int position);         // the Mode knob's order (ui.c MODE_ORDER)
const char* laneName(int lane);

bool playing();
void play(bool on);               // PLAY

// Readouts for the screen.
int view();
const char* viewName(int view);
bool perform();
bool locked();
bool soloed();
bool held();
bool external();
bool dirty();
int slot();
uint32_t effectsHeld();   // EN_FX_* held (grid or black keys), EN_MV_* << 16
int leadDegree();         // the Lead's scale degree sounding, -1 none
int numViews();           // the FM-1's pages (ui.c NVIEWS); viewName() names each

// The chord sounding, as a number (0: none; 1..kChordLabelBase-1: a root and its other pitch classes;
// from kChordLabelBase: a written slot's root, kind and type). chordCode() runs on the audio thread;
// chordText() makes the FM-1's name of it ("Em7", "C7sus4") on any thread, reading only constants.
constexpr int kChordLabelBase = 1 + 12 * 4096;
constexpr int kChordCodes = kChordLabelBase + 12 * 7 * 7;   // (EN_NCQ, EN_NCX)
constexpr int kChordTextMax = 16;
int chordCode();
void chordText(int code, char* buf);   // buf: kChordTextMax; "" for 0 or a code out of range
const char* chordNow(char* buf);       // (tests) ui.c's own chord_now()

// The MPC's own controls for what the FM-1 does with gestures (docs/superpowers/specs/2026-11-20-third-planet-
// controls-design.md). Each calls the code in ui.c that the FM-1's gesture calls, and counts as the player acting
// (autosave). Setters are absolute: setting what already is does nothing.
constexpr int kNumLayers = 5;     // Drone, Sequence, Lead, Sequence 2, Atmosphere (PERFORM's mutes and solos)
constexpr int kNumNudge = 6;      // Drone, Sequence, Lead, Sequence 2, Space, Atmosphere (ui.c NUDGE_*)
constexpr int kNumSteps = 16;
constexpr int kNumFx = 16;        // the effects grid, in its order (EN_FX_*)
constexpr int kNumMoves = 8;      // Rise, Close, Transpose (the black keys'), Sweep, Open, Shut, Drop, Echo (PERFORM's)
void setHold(bool on);            // REC tapped: en_freeze (the engine takes it at its next block; held() follows)
void setLock(bool on);            // SEL held: lock_toggle
bool muted(int layer);
void setMute(int layer, bool on); // PERFORM's white keys 1-5
bool soloed(int layer);
void setSolo(int layer, bool on); // PERFORM's black keys 1-5
void forget();                    // PERFORM's top key: en_lead_forget
void nextChord();                 // EDIT held: en_next
void newChords();                 // EDIT held on: en_prog_rewalk
void shakeAll();                  // HOME held on: every layer shaken, and the noise
void resetPiece();                // GLO held on: the piece back to its recipe (not saved)
void nudgeLayer(int layer, bool shake);   // a layer's button held (a nudge), held on (a shake); layer < kNumNudge
bool step(int st);                // the Pattern page's lane (knob Lane): its step st, as it plays
void setStep(int st, bool on);    // a white key there (pattern_key)
bool accent(int st);              // the lane's sequence's accent on step st
void setAccent(int st, bool on);  // a black key held with the white one: accented, and the step on
int chordRoot();                  // the Chords page's slot (knob Slot): its root 0..11 (C..B), -1 a gap
void setChordRoot(int root);      // a key there (slot_key); -1: the slot a gap (its kind Empty)
// Pads, held: a scale degree of the Lead (0..15), an effect of the grid, a move. A release of what is not held,
// or of a degree the Lead no longer plays, does nothing.
void padLead(int degree, bool down);
void padFx(int fx, bool down);
void padMove(int move, bool down);
uint32_t perfMoves();             // PERFORM's moves held (EN_PF_*)
const char* fxName(int fx);
const char* moveName(int move);

// Readouts for the star-chart screen (docs/superpowers/specs/2026-11-20-third-planet-screen-design.md). Each reads
// what the engine and ui.c already keep; nothing changes. An index out of range reads 0 (or the empty state).
int section();                    // the arc's section, EN_SEC_* (0..3; EN_SEC_NAME names it)
int phraseBar();                  // the bar of the phrase, 0..7
int evolveChance();               // percent of phrases Evolve nudges now (ui.c nudge_tick); 0 while locked, held or resting
float layerActivity(int layer);   // how much the layer sounds now, 0..1 (layer < kNumLayers)
bool layerHeard(int layer);       // the layer not muted and not soloed out (ui.c perf_sounds)
float filterDrift(int layer);     // the layer's filter drift, in octaves
int densityShift();               // Density's slow drift, in its units (+ denser, - sparser)
int filterHeading(int layer);     // which way the filter drift goes (en_fo_dir): +1 up, -1 down, 0 settled. It moves
                                  // further out while this has filterDrift()'s sign (ui.c move_state)
int densityHeading();             // which way Density's drift goes (en_dd_dir): +1 further from the knob's value,
                                  // -1 back towards it, 0 settled
uint32_t seqHits(int seq);        // each sequence's notes so far (seq 0..1)
uint32_t seqReps(int seq);        // each sequence's repeats so far
int ringDots(bool* lastBar);      // ui.c ring_dots: the chord's progress in dots, 0..32, -1 no ring (Drift Hold);
                                  // *lastBar: the chord's last bar
int progPos();                    // the slot the progression is on (0..7; Endless: 0), -1 none
bool loopMode();                  // Loop (the slots in turn), else Endless
int progCount();                  // how far the slots run in Loop: to the last that isn't a gap (ui.c prog_count)
int walkCharacter();              // the walk's character (EN_WALK_*)
enum SlotState { kSlotGap, kSlotWalk, kSlotWritten };
SlotState slotState(int i);       // the Chords page's slot i (0..7)
int slotChordCode(int i);         // slot i's chord in chordCode()'s numbers, named as ui.c slot_label names it
                                  // (written: its root, kind and type; the walk's: its pitch classes); 0 for a gap
int nextChordCode();              // the next chord that will sound (Endless: the first slot from 1 that isn't a gap;
                                  // Loop: the first after progPos() that isn't a gap, going round); 0 none
int barsToChange();               // bars to the engine's next change, the one playing counted (1: this is the last);
                                  // -1 at Drift Hold. A change onto a gap holds the chord. While held() the engine
                                  // counts on but does not change: this reaches 0 and stays there (1 once let go)
int barsToNextChord();            // bars until nextChordCode() sounds, the one playing counted: barsToChange() and
                                  // Drift's bars for each gap changed onto before it. -2 while held() (changes
                                  // paused; before the others); -1 at Drift Hold or with no next chord
int loopStep(int seq);            // the step a sequence plays (seq 0..1)
const char* slotLabel(int i, char* buf);   // (tests) ui.c's own slot_label(); buf: kChordTextMax

// The log for the star-chart screen: who did what, so a nudge, a shake and Evolve's own nudges can be told apart.
//   You     the actions above (setHold .. nudgeLayer), one event each; a nudge or a shake is followed by one
//           kEvParam for each value it moved, and a shake's new voice (the Space's: its echo time) is a kEvVoice.
//           shakeAll() is the one kEvShakeAll, its values not listed. Setting what already is logs nothing.
//   Evolve  each value ui.c nudge_tick moved, on a phrase's first bar: kEvParam.
//   Piece   the piece changed (setPiece, or PRESET in the app): one kEvPiece. Evolve's values of the same frame, or
//           of the frame after setPiece, are dropped with it.
// Not logged: knob(), voice() and walk() (the MPC's knobs show themselves), pads, steps and slots, Motion's moves,
// the walk's slots, the virtual panel's own gestures, boot() (which empties the log).
// layer: kEvMute and kEvSolo, the layer (< kNumLayers); kEvNudge, kEvShake, kEvParam and kEvVoice, the nudge's layer
// (< kNumNudge: 4 the Space, 5 the Atmosphere); else -1. param: kEvParam and kEvVoice, the parameter (P_*); else -1.
// from, to: a parameter's values; kEvHold, kEvLock, kEvPlay, kEvMute and kEvSolo, off and on (0, 1); kEvPiece, the
// pieces; else 0.
enum Who : uint8_t { kWhoYou, kWhoEvolve, kWhoPiece };
enum What : uint8_t { kEvParam, kEvVoice, kEvHold, kEvLock, kEvPlay, kEvForget, kEvNextChord,
                      kEvNewChords, kEvShakeAll, kEvReset, kEvMute, kEvSolo, kEvNudge, kEvShake, kEvPiece };
struct Event { Who who; What what; int8_t layer; int16_t param; int16_t from, to; };
constexpr int kNumEvents = 64;
int takeEvents(Event* out, int max);   // the oldest first, up to max, taken out of the ring (kNumEvents entries;
                                       // the oldest dropped on overflow). On the audio thread, as everything here

// The project as the FM-1 would save it (bytes, or -1 if max is too small).
int snapshot(uint8_t* dst, int max);

} // namespace app
} // namespace tp
