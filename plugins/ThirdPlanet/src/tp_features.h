// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: build switches. (Not "features.h": glibc has one, and src/ is on the include path.)
//
// TP_LINK: Harmony Link, Third Planet leading the harmony of a sequencer on the computer over MIDI (docs/harmony-link.md).
// Off by default: the default build is the plugin without it. On with --define TP_LINK=1
// (tools/build_variant.sh on). Off, the plugin has the same parameters, skin, saved project and sound as the
// plugin before Harmony Link existed (tests/link_null.sh).
#pragma once

#ifndef TP_LINK
#define TP_LINK 0
#endif
