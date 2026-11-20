// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: the FM-1's power switch.
//
// On the FM-1 a power-on starts every variable of the engine and the app from its initial value (the
// device zeroes its RAM and copies its data in). en_init() and ui_init() alone do not: some state lives
// on (amb_host.c's "reboot" has the same property). The plugin boots the FM-1 again whenever a project
// loads or another instance takes the engine over, so it needs the real thing.
//
// Every variable of NACHTFAHRT's code (engine_unit.cpp, app_unit.cpp) is placed in two sections of its
// own with `#pragma clang section`: TP_SECTIONS_BEGIN / TP_SECTIONS_END around the includes. At load the
// initialised one is copied; power_on() copies it back and zeroes the other, before every boot.
#pragma once

#if defined(__APPLE__)
#define TP_SECTIONS_BEGIN _Pragma("clang section bss=\"__DATA,__tp_bss\" data=\"__DATA,__tp_data\"")
#else
#define TP_SECTIONS_BEGIN _Pragma("clang section bss=\"tp_bss\" data=\"tp_data\"")
#endif
#define TP_SECTIONS_END _Pragma("clang section bss=\"\" data=\"\"")

namespace tp {
// Every NACHTFAHRT variable back to its power-on value. Only while owning the engine.
void power_on();
// The two sections' sizes in bytes (for tests).
long power_data_bytes();
long power_bss_bytes();
} // namespace tp
