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
#include "tp_features.h"

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
    Link,      // Third Planet's own: Harmony Link, lead the link receiver's harmony (TP_LINK; the parameter only then)
    KeyMode,   // Third Planet's own: what MIDI notes play (Pads: the three pad banks; Lead: the Lead)
    PadBase,   // Third Planet's own: the pad banks' first note
    Switch,    // a two-way switch for an FM-1 state (code: Sw*): tp::app's setter and its readout
    Action,    // a one-shot (code: Act*): acts once on a change to On, then goes back to Off
    Step,      // the Pattern lane's step (code: 0..15)
    Accent,    // the Pattern lane's accent (code: 0..15)
    ChordRoot, // the Chords slot's root: a gap or C..B
};

// Switch codes: Hold, Lock, Play, then the five layers' mutes and solos.
enum SwitchCode { kSwHold, kSwLock, kSwPlay, kSwMute0, kSwSolo0 = kSwMute0 + tp::app::kNumLayers };
// Action codes: the five, then each layer's nudge and shake (Drone, Seq, Lead, Seq 2, Space, Atmos).
enum ActionCode { kActForget, kActNext, kActNewChords, kActShakeAll, kActReset, kActNudge0,
                  kActShake0 = kActNudge0 + tp::app::kNumNudge };

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
#if TP_LINK
    {"link", "Link", Kind::Link, 0},
#endif
    {"keymode", "Keys", Kind::KeyMode, 0},
    {"pad_base", "Pad base", Kind::PadBase, 0},
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
    {"chord_root", "Chord root", Kind::ChordRoot, 0},
    // PLAY: the FM-1's gestures as switches and one-shots
    {"hold", "Hold", Kind::Switch, kSwHold},
    {"lock", "Lock", Kind::Switch, kSwLock},
    {"play", "Play", Kind::Switch, kSwPlay},
    {"forget", "Forget", Kind::Action, kActForget},
    {"next_chord", "Next chord", Kind::Action, kActNext},
    {"new_chords", "New chords", Kind::Action, kActNewChords},
    {"shake_all", "Shake all", Kind::Action, kActShakeAll},
    {"reset_piece", "Reset piece", Kind::Action, kActReset},
    // LAYERS
    {"mute_drone", "Mute Drone", Kind::Switch, kSwMute0 + 0},
    {"mute_seq", "Mute Seq", Kind::Switch, kSwMute0 + 1},
    {"mute_lead", "Mute Lead", Kind::Switch, kSwMute0 + 2},
    {"mute_seq2", "Mute Seq 2", Kind::Switch, kSwMute0 + 3},
    {"mute_atmos", "Mute Atmos", Kind::Switch, kSwMute0 + 4},
    {"solo_drone", "Solo Drone", Kind::Switch, kSwSolo0 + 0},
    {"solo_seq", "Solo Seq", Kind::Switch, kSwSolo0 + 1},
    {"solo_lead", "Solo Lead", Kind::Switch, kSwSolo0 + 2},
    {"solo_seq2", "Solo Seq 2", Kind::Switch, kSwSolo0 + 3},
    {"solo_atmos", "Solo Atmos", Kind::Switch, kSwSolo0 + 4},
    {"nudge_drone", "Nudge Drone", Kind::Action, kActNudge0 + 0},
    {"nudge_seq", "Nudge Seq", Kind::Action, kActNudge0 + 1},
    {"nudge_lead", "Nudge Lead", Kind::Action, kActNudge0 + 2},
    {"nudge_seq2", "Nudge Seq 2", Kind::Action, kActNudge0 + 3},
    {"nudge_space", "Nudge Space", Kind::Action, kActNudge0 + 4},
    {"nudge_atmos", "Nudge Atmos", Kind::Action, kActNudge0 + 5},
    {"shake_drone", "Shake Drone", Kind::Action, kActShake0 + 0},
    {"shake_seq", "Shake Seq", Kind::Action, kActShake0 + 1},
    {"shake_lead", "Shake Lead", Kind::Action, kActShake0 + 2},
    {"shake_seq2", "Shake Seq 2", Kind::Action, kActShake0 + 3},
    {"shake_space", "Shake Space", Kind::Action, kActShake0 + 4},
    {"shake_atmos", "Shake Atmos", Kind::Action, kActShake0 + 5},
    // PATTERN: the lane's steps and accents
    {"step1", "Step 1", Kind::Step, 0}, {"step2", "Step 2", Kind::Step, 1}, {"step3", "Step 3", Kind::Step, 2},
    {"step4", "Step 4", Kind::Step, 3}, {"step5", "Step 5", Kind::Step, 4}, {"step6", "Step 6", Kind::Step, 5},
    {"step7", "Step 7", Kind::Step, 6}, {"step8", "Step 8", Kind::Step, 7}, {"step9", "Step 9", Kind::Step, 8},
    {"step10", "Step 10", Kind::Step, 9}, {"step11", "Step 11", Kind::Step, 10}, {"step12", "Step 12", Kind::Step, 11},
    {"step13", "Step 13", Kind::Step, 12}, {"step14", "Step 14", Kind::Step, 13}, {"step15", "Step 15", Kind::Step, 14},
    {"step16", "Step 16", Kind::Step, 15},
    {"accent1", "Accent 1", Kind::Accent, 0}, {"accent2", "Accent 2", Kind::Accent, 1},
    {"accent3", "Accent 3", Kind::Accent, 2}, {"accent4", "Accent 4", Kind::Accent, 3},
    {"accent5", "Accent 5", Kind::Accent, 4}, {"accent6", "Accent 6", Kind::Accent, 5},
    {"accent7", "Accent 7", Kind::Accent, 6}, {"accent8", "Accent 8", Kind::Accent, 7},
    {"accent9", "Accent 9", Kind::Accent, 8}, {"accent10", "Accent 10", Kind::Accent, 9},
    {"accent11", "Accent 11", Kind::Accent, 10}, {"accent12", "Accent 12", Kind::Accent, 11},
    {"accent13", "Accent 13", Kind::Accent, 12}, {"accent14", "Accent 14", Kind::Accent, 13},
    {"accent15", "Accent 15", Kind::Accent, 14}, {"accent16", "Accent 16", Kind::Accent, 15},
};

constexpr int kNumControls = (int)(sizeof(kParams) / sizeof(kParams[0]));

} // namespace tp
