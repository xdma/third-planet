// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: build switches. (Not "features.h": glibc has one, and src/ is on the include path.)
//
// TP_LINK: Harmony Link, Third Planet leading Performer's harmony over MIDI
// (docs/superpowers/specs/2026-11-21-harmony-link-design.md). On by default when its files are present (src/link.h);
// off with --define TP_LINK=0, or by deleting src/link.*. Off, the plugin is the plugin without it: the same
// parameters, skin, saved project and sound (tests/link_null.sh).
#pragma once

#ifndef TP_LINK
#if defined(__has_include)
#if __has_include("link.h")
#define TP_LINK 1
#else
#define TP_LINK 0
#endif
#else
#define TP_LINK 0
#endif
#endif
