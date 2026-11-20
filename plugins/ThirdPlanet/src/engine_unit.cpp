// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: NACHTFAHRT's engine (src/nachtfahrt/, by Cranky Man, GPL-3.0-only), compiled here as one
// translation unit. The SDK's build compiles only src/*.cpp as C++17, so the C engine is included from this
// file. Upstream builds with -ffp-contract=off (no fused multiply-add); the pragma below does the same, so
// the floating-point results match upstream's builds sample for sample. EN_HOST is upstream's non-device
// build (the emulator, the host simulator): it only adds test readouts and drops the device's RAM section.
#define EN_HOST 1
#pragma clang fp contract(off)

#include "power.h"

extern "C" {
TP_SECTIONS_BEGIN   // every engine variable, for power_on() (power.h)
#include "nachtfahrt/dsp/amb.c"
TP_SECTIONS_END

// The MPC host discards MIDI out from plugins: the engine's MIDI out goes nowhere.
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st, (void)d1, (void)d2; }
}
