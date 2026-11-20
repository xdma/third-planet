// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: the MPC's parameters, and which FM-1 control each one is.
//
// Every FM-1 knob that sets a sound is an MPC parameter, absolute, so a Q-Link or the touch screen sets
// it directly (the FM-1's knobs are relative and change meaning with the page; the MPC's do not).
// A write goes through the same path the FM-1's knob uses (ui.c knob_set, set_voice, set_piece), so
// whatever follows from a turn follows here too. Values the app changes by itself (a piece, Evolve's
// nudges, a shake, the engine's macros) come back to the MPC.
//
// Order: the first 16 are the default Q-Links (the Nachtfahrt and Perform knobs, the key, the levels),
// then each FM-1 page in panel order. Parameter 0 is Volume (the host nudges parameter 0 on every load).
#pragma once

#include "app_api.h"

extern "C" {
#include "nachtfahrt/dsp/amb.h"
}

namespace tp {

enum class Kind
{
    Master,    // the MASTER pot
    Knob,      // an FM-1 knob: tp::app::knob(code) / knobValue(code)
    Voice,     // a layer's voice (ALGO): tp::app::voice(code)
    Piece,     // the piece (PRESET)
    Walk,      // the Chords page's walk character (ALGO there)
    Sync,      // Third Planet's own: follow the MPC's tempo and transport (MIDI clock to the FM-1)
    KeyMode,   // Third Planet's own: how MIDI notes reach the FM-1
};

struct ParamDef
{
    const char* id;     // saved in projects
    const char* name;   // shown on the MPC (nullptr: the engine's own name)
    Kind kind;
    int code;           // P_* or tp::app::KnobCode (Knob, Voice)
};

using namespace tp::app;

constexpr ParamDef kParams[] = {
    // the default Q-Links
    {"volume", "Volume", Kind::Master, 0},
    {"filter", "Filter", Kind::Knob, kKnobFilter},
    {"evolve", nullptr, Kind::Knob, P_EVOLVE},
    {"motion", nullptr, Kind::Knob, P_MOTION},
    {"drift", nullptr, Kind::Knob, P_DRIFT},
    {"energy", "Energy", Kind::Knob, kKnobEnergy},
    {"wet", "Wet", Kind::Knob, kKnobWet},
    {"sweep", "Sweep", Kind::Knob, P_SWEEP},
    {"tempo", nullptr, Kind::Knob, P_TEMPO},
    {"root", nullptr, Kind::Knob, P_ROOT},
    {"mode", "Mode", Kind::Knob, kKnobMode},
    {"transpose", nullptr, Kind::Knob, P_TRANS},
    {"lvl_drone", "Drone", Kind::Knob, P_CHORD},
    {"lvl_seq", "Sequence", Kind::Knob, P_SEQ},
    {"lvl_lead", "Lead", Kind::Knob, P_LEAD},
    {"lvl_seq2", "Sequence 2", Kind::Knob, P_SEQ2},
    // the piece, and Third Planet's own
    {"piece", "Piece", Kind::Piece, 0},
    {"sync", "Sync", Kind::Sync, 0},
    {"keymode", "Keys", Kind::KeyMode, 0},
    {"lvl_atmos", "Atmosphere", Kind::Knob, P_CRACKLE},
    {"tune", nullptr, Kind::Knob, P_TUNE},
    // Drone (ENV)
    {"drone_voice", "Drone voice", Kind::Voice, P_CVOICE},
    {"pedal", nullptr, Kind::Knob, P_PEDAL},
    {"drone_reso", "Drone reso", Kind::Knob, P_CRES},
    {"phaser", nullptr, Kind::Knob, P_PHASER},
    {"drone_attack", "Drone attack", Kind::Knob, P_CATT},
    {"drone_colour", "Drone colour", Kind::Knob, P_CCOL},
    {"drone_release", "Drone release", Kind::Knob, P_DREL},
    {"drone_reverb", nullptr, Kind::Knob, P_DRREV},
    {"drone_echo", nullptr, Kind::Knob, P_DRECHO},
    {"drone_width", nullptr, Kind::Knob, P_MW0},
    {"drone_time", nullptr, Kind::Knob, P_MT0},
    {"drone_rate", nullptr, Kind::Knob, P_MR0},
    {"drone_intro", nullptr, Kind::Knob, P_MI0},
    // Sequence (ARP)
    {"seq_voice", "Seq voice", Kind::Voice, P_SVOICE},
    {"density", nullptr, Kind::Knob, P_DENSITY},
    {"seq_oct", "Seq octave", Kind::Knob, P_SEQOCT},
    {"seq_len", "Seq length", Kind::Knob, P_SEQLEN},
    {"seq_grid", "Seq grid", Kind::Knob, P_SEQGRID},
    {"seq_cutoff", "Seq cutoff", Kind::Knob, P_CUTOFF},
    {"seq_decay", "Seq decay", Kind::Knob, P_DECAY},
    {"seq_reverb", nullptr, Kind::Knob, P_SQREV},
    {"seq_echo", nullptr, Kind::Knob, P_SQECHO},
    {"repeat_a", nullptr, Kind::Knob, P_TAPA},
    {"repeat_a_time", "A time", Kind::Knob, P_TAPAT},
    {"repeat_b", nullptr, Kind::Knob, P_TAPB},
    {"repeat_b_time", "B time", Kind::Knob, P_TAPBT},
    {"seq_time", nullptr, Kind::Knob, P_MT1},
    {"seq_rate", nullptr, Kind::Knob, P_MR1},
    {"seq_intro", nullptr, Kind::Knob, P_MI1},
    // Lead (LFO)
    {"lead_voice", "Lead voice", Kind::Voice, P_LVOICE},
    {"phrases", nullptr, Kind::Knob, P_LDENS},
    {"lead_oct", "Lead octave", Kind::Knob, P_LOCT},
    {"glide", nullptr, Kind::Knob, P_GLIDE},
    {"vibrato", nullptr, Kind::Knob, P_VIB},
    {"lead_tone", nullptr, Kind::Knob, P_LTONE},
    {"lead_attack", nullptr, Kind::Knob, P_LATT},
    {"lead_reverb", nullptr, Kind::Knob, P_LDREV},
    {"lead_echo", nullptr, Kind::Knob, P_LDECHO},
    {"lead_width", nullptr, Kind::Knob, P_MW2},
    {"lead_time", nullptr, Kind::Knob, P_MT2},
    {"lead_rate", nullptr, Kind::Knob, P_MR2},
    {"lead_intro", nullptr, Kind::Knob, P_MI2},
    // Sequence 2 (SEQ), its Pattern page
    {"seq2_voice", "Seq2 voice", Kind::Voice, P_S2VOICE},
    {"seq2_chance", "Seq2 chance", Kind::Knob, P_SCH1},
    {"seq2_oct", nullptr, Kind::Knob, P_SEQ2OCT},
    {"seq2_len", "Seq2 length", Kind::Knob, P_SEQ2LEN},
    {"seq2_cutoff", "Seq2 cutoff", Kind::Knob, P_S2CUT},
    {"seq2_decay", "Seq2 decay", Kind::Knob, P_S2DEC},
    {"seq2_reverb", nullptr, Kind::Knob, P_S2REV},
    {"seq2_echo", nullptr, Kind::Knob, P_S2ECHO},
    {"seq2_width", nullptr, Kind::Knob, P_MW3},
    {"seq2_time", nullptr, Kind::Knob, P_MT3},
    {"seq2_rate", nullptr, Kind::Knob, P_MR3},
    {"seq2_intro", nullptr, Kind::Knob, P_MI3},
    {"lane", "Lane", Kind::Knob, kKnobLane},
    {"accent", nullptr, Kind::Knob, P_SACCENT},
    {"lane_chance", "Lane chance", Kind::Knob, kKnobLaneChance},
    {"seq_play", "Pattern", Kind::Knob, P_SMODE},
    // Atmosphere (SAVE)
    {"atmos_type", "Atmos type", Kind::Knob, P_NCHAR},
    {"atmos_density", "Atmos density", Kind::Knob, P_NGRAIN},
    {"atmos_spread", "Atmos spread", Kind::Knob, P_NWIDTH},
    {"atmos_drift", "Atmos drift", Kind::Knob, P_NDRIFT},
    {"atmos_air", "Atmos air", Kind::Knob, P_NROOM},
    {"atmos_colour", "Atmos colour", Kind::Knob, P_NCOL},
    {"atmos_ring", "Atmos ring", Kind::Knob, P_NTUNE},
    {"atmos_reverb", "Atmos reverb", Kind::Knob, P_NPLATE},
    {"atmos_echo", "Atmos echo", Kind::Knob, P_NECHO},
    {"atmos_width", "Atmos width", Kind::Knob, P_MW4},
    {"atmos_time", nullptr, Kind::Knob, P_MT4},
    {"atmos_rate", nullptr, Kind::Knob, P_MR4},
    {"atmos_intro", nullptr, Kind::Knob, P_MI4},
    // Space and Echo (FX)
    {"echo", nullptr, Kind::Knob, P_ECHO},
    {"echo_time", nullptr, Kind::Knob, P_ETIME},
    {"feedback", nullptr, Kind::Knob, P_FEEDBACK},
    {"reverb", nullptr, Kind::Knob, P_REVERB},
    {"echo_blur", "Echo blur", Kind::Knob, P_EBLUR},
    {"echo_tone", nullptr, Kind::Knob, P_ETONE},
    {"echo_width", "Echo width", Kind::Knob, P_EWIDTH},
    {"plate_size", "Plate size", Kind::Knob, P_SIZE},
    // Arc (GLO)
    {"lead_in", nullptr, Kind::Knob, P_LDIN},
    {"bridge", nullptr, Kind::Knob, P_BRIDGE},
    {"arc", nullptr, Kind::Knob, P_ARC},
    {"memory", nullptr, Kind::Knob, P_LMEM},
    // Chords (EDIT)
    {"chord_play", "Chord play", Kind::Knob, kKnobLoop},
    {"walk", "Walk", Kind::Walk, 0},
    {"chord_slot", "Chord slot", Kind::Knob, kKnobSlot},
    {"chord_kind", "Chord kind", Kind::Knob, kKnobChord},
    {"chord_type", "Chord type", Kind::Knob, kKnobType},
};

constexpr int kNumControls = (int)(sizeof(kParams) / sizeof(kParams[0]));

} // namespace tp
