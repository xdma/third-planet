// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: Harmony Link's capture (docs/superpowers/specs/2026-11-21-harmony-link-design.md, 5.2, 5.4 and 5.5).
// On the audio thread, once a block, from watch(): no allocation, no locks, no I/O. Everything is read from the
// tp::app readouts (app_api.h) as they stand; nothing new is computed in the engine.
// (Defined in link_capture.cpp only with TP_LINK=1.)
#pragma once

#include <cstdint>

#include "app_api.h"
#include "link_codec.h"

namespace mpc {
struct Transport;
}

namespace tp::link {

// The plan now. timed = tp::app::external(); pos16 = the host's position in 16ths since its last bar line
// (ppqPos - barStartPpq, 0..15; 0 with no position). Chord names are chordText()'s first 8 bytes, NUL-padded.
PlanState capture(const mpc::Transport&);
// Spec 5.5: every kWhoYou event, and kEvPiece; not Evolve's kEvParam and kEvVoice (their effect shows in the Plan).
bool forwards(const tp::app::Event&);
// A chordCode() number as absolute pitch classes (bit n = pitch class n). A written slot's (its root, kind and
// type) through the engine's own voicing, en_chord_iv(); a root and its pitch classes as they are; 0: none.
uint16_t chordMask(int code);

} // namespace tp::link
