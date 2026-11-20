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

// The project as the FM-1 would save it (bytes, or -1 if max is too small).
int snapshot(uint8_t* dst, int max);

} // namespace app
} // namespace tp
