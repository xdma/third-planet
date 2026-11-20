// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: the star-chart screen's model (docs/superpowers/specs/2026-11-20-third-planet-screen-design.md,
// section 8.1). It turns the app's readouts and its event log into what the SKY tab shows: the orbit particles,
// the five planets' frames and the three log lines. Pure: no app, no heap, no locks or I/O; it runs on the audio
// thread. The plugin feeds it a SkyInput and sends what it reads back as display parameters.
#pragma once

#include <cstdint>

#include "app_api.h"

namespace tp {
namespace sky {

// What the model reads, one snapshot per advance(). Planet layers (heard, act) are the five mute layers: Drone,
// Seq, Lead, Seq 2, Atmos. events: what tp::app::takeEvents() handed out since the last call, the oldest first.
// piece: the piece playing (tp::app::piece()), for the reset's log line.
struct SkyInput
{
    bool playing, held;
    uint32_t hits[2], reps[2];
    bool heard[5];
    float act[5];
    const tp::app::Event* events;
    int numEvents;
    int piece;
};

constexpr int kPucks = 8;            // 0..3 Sequence (inner orbit), 4..7 Sequence 2 (outer), as ui.c ORB_RX0/RX1
constexpr int kLogLines = 3;
constexpr int kTextMax = 32;         // incl. NUL: 31 bytes of text
constexpr int kPlanets = 5;          // Drone, Seq, Lead, Seq 2, Atmos (the mute layers)
constexpr int kFlashMs = 600;

// A particle in its orbit's pad, 0..1 each way: x = 0.5 + 0.5 cos(angle), y = 0.5 + 0.5 sin(angle), y growing
// downwards as on the FM-1's screen (ui.c draw_orbits). Parked = {0.5f, 0.5f}, behind the chord's disc.
struct Puck { float x, y; };

class Sky
{
public:
    Sky() { reset(); }
    void reset();                                     // all parked, log empty, no flashes
    void advance(const SkyInput& in, int ms);         // ms since the last call
    Puck puck(int i) const;
    int planetFrame(int layer) const;                 // 0..23: level (0..7) x 3 + state (0 heard, 1 muted, 2 flash)
    int logSerial(int line) const;                    // changes whenever that line's text changes
    const char* logText(int line) const;              // line 0 newest; "" if empty
    int flash(int layer) const;                       // ms left of the nudge flash, layer 0..5 (the nudge layers)

private:
    struct Orb { float a, life; bool on; };
    Orb orb_[kPucks];
    uint32_t seen_hits_[2], seen_reps_[2];
    int tick_ms_;                                     // ms toward the next 48 ms step
    float act_[kPlanets];
    bool heard_[kPlanets];
    int flash_[tp::app::kNumNudge];
    char text_[kLogLines][kTextMax];
    int serial_[kLogLines];
    uint32_t serial_next_ = 1;                        // never reset: a serial is never reused

    void launch(int seq, uint32_t count, bool rep);
    void push(const tp::app::Event& e, int piece);
    int nextSerial();
};

// One event as a log line: "Evolve › Seq Cutoff +6", "You › Shake Lead FM Reed", "Piece › Phaedra". Writes at most
// kTextMax - 1 bytes and a NUL to out (kTextMax bytes), never cutting a UTF-8 sequence; returns the length.
// piece: the piece playing, which kEvReset names.
int formatEvent(const tp::app::Event& e, char* out, int piece = 0);

// A parameter's short name in the log ("Cutoff", "Tone"), without its layer (the line names that); "" out of range.
const char* shortName(int param);

} // namespace sky
} // namespace tp
