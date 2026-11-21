// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: build switches. (Not "features.h": glibc has one, and src/ is on the include path.)
//
// TP_LINK: Harmony Link, Third Planet leading the harmony of a sequencer on the computer over MIDI
// (docs/superpowers/specs/2026-11-21-harmony-link-design.md). On by default when its marker is present
// (src/tp_link.h; not "link.h": glibc has one, and __has_include would find it on the MPC's toolchains);
// off with --define TP_LINK=0, or by deleting src/tp_link.h (and the link's sources). Off, the plugin is the plugin without it: the same
// parameters, skin, saved project and sound (tests/link_null.sh).
#pragma once

#ifndef TP_LINK
#if defined(__has_include)
#if __has_include("tp_link.h")
#define TP_LINK 1
#else
#define TP_LINK 0
#endif
#else
#define TP_LINK 0
#endif
#endif
