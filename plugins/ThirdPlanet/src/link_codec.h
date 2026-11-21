// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: Harmony Link v1's wire codec. The format is open and written down in docs/harmony-link.md (golden
// vectors: docs/harmony-link-vectors.txt). Pure functions: no allocation, no I/O, no state, so the audio thread may
// call the encoders. Messages are SysEx: F0 7D 54 50 <ver 01> <type> <seq> <payload...> F7, every byte between F0
// and F7 7-bit. (Defined in link_codec.cpp only with TP_LINK=1.)
#pragma once

#include <cstdint>

namespace tp::link {

constexpr uint8_t kVersion = 0x01;
enum MsgType : uint8_t { kPlan = 0x01, kEvent = 0x02, kBye = 0x03, kFade = 0x10, kHold = 0x11 };

struct Chord {
    uint8_t root = 127;   // 0..11 (C..B); 127 none
    uint16_t mask = 0;    // absolute pitch classes, bit = pc (bits 0..11)
    char name[8] = {};    // ASCII, NUL-padded; all 8 used means no NUL
};

struct PlanState {
    bool playing, held, locked, loopMode, timed;
    uint8_t piece, keyRoot, keyMode;
    Chord now, next;
    int8_t barsToNext, barsToChange;          // as app_api.h (-2 held, -1 none)
    int8_t progPos; uint8_t progCount, walk;  // progPos -1..7
    uint8_t slotState[8]; Chord slot[8];
    uint8_t section, phraseBar, evolveChance; int8_t densityShift;
    uint8_t heardMask; uint8_t activity[5];   // 0..127
    uint8_t pos16;                            // 0..15
    bool operator==(const PlanState&) const;  // memberwise; used for change detection
};

struct LinkEvent { uint8_t who, what; int8_t layer; int16_t param, from, to; };

struct Command {
    enum Type : uint8_t { None, Fade, Hold } type = None;
    uint8_t level = 0;    // Fade: 0..127
    uint16_t bars = 0;    // Fade: 0..16383
    bool hold = false;    // Hold: on
};

constexpr int kMaxMsg = 160;
constexpr int kPlanLen = 146;    // 7 header + 138 payload + F7
constexpr int kEventLen = 17;    // 7 + 9 + 1
constexpr int kByeLen = 8;

// Each writes one whole message to out (room for kMaxMsg) and returns its length. Out-of-range values are clamped
// (seq wraps: its low 7 bits), so every byte between F0 and F7 is 7-bit whatever the input.
int encodePlan(const PlanState&, uint8_t seq, uint8_t* out);    // kPlanLen
int encodeEvent(const LinkEvent&, uint8_t seq, uint8_t* out);   // kEventLen
int encodeBye(uint8_t seq, uint8_t* out);                       // kByeLen
// A FADE or HOLD of v1, exactly as docs/harmony-link.md has it; anything else (other types, versions, lengths, a
// byte over 7F, HOLD not 0 or 1) is false and leaves out alone. len is checked before any byte is read.
bool decodeCommand(const uint8_t* msg, int len, Command& out);
bool isLinkSysex(const uint8_t* msg, int len);                  // starts F0 7D 54 50

} // namespace tp::link
