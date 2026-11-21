// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: Harmony Link's capture on the audio thread (link_capture.h).
#include "tp_features.h"

#if TP_LINK

#include "link_capture.h"

#include <cmath>

#include "engine.h"
#include "mpc_plugin.h"

namespace tp::link {

namespace {

uint8_t clampU8(int v, int hi) { return (uint8_t)(v < 0 ? 0 : v > hi ? hi : v); }
int8_t clampS8(int v) { return (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v); }

Chord chord(int code)
{
    using namespace tp::app;
    Chord c{};
    if (code >= kChordLabelBase && code < kChordCodes)
        c.root = (uint8_t)((code - kChordLabelBase) / (EN_NCQ * EN_NCX));
    else if (code >= 1 && code < kChordLabelBase)
        c.root = (uint8_t)((code - 1) / 4096);
    else
        return c;   // (none: root 127, no mask, no name)
    c.mask = chordMask(code);
    char text[kChordTextMax] = {};
    chordText(code, text);
    for (int i = 0; i < 8 && text[i] != 0; ++i)   // (8 used: no NUL, as the format has it)
        c.name[i] = text[i];
    return c;
}

uint8_t pos16(const mpc::Transport& t)
{
    if (!t.valid || !t.ppqValid) return 0;
    const double into = (t.ppqPos - t.barStartPpq) * 4.0;   // (16ths; not a number fails both tests)
    if (!(into >= 0.0)) return 0;
    if (!(into < 15.0)) return 15;
    return (uint8_t)into;
}

} // namespace

uint16_t chordMask(int code)
{
    using namespace tp::app;
    if (code >= kChordLabelBase && code < kChordCodes)
    {
        const int c = code - kChordLabelBase, root = c / (EN_NCQ * EN_NCX);
        int iv[4];
        en_chord_iv(c / EN_NCX % EN_NCQ, c % EN_NCX, iv);
        uint16_t m = 0;
        for (int k = 0; k < 4; ++k)
            m |= (uint16_t)(1u << ((root + iv[k]) % 12));
        return m;
    }
    if (code >= 1 && code < kChordLabelBase)
    {
        const int root = (code - 1) / 4096, iv = (code - 1) % 4096;
        uint16_t m = (uint16_t)(1u << root);
        for (int b = 0; b < 12; ++b)
            if (iv >> b & 1) m |= (uint16_t)(1u << ((root + b) % 12));
        return m;
    }
    return 0;
}

bool forwards(const tp::app::Event& e) { return e.who == tp::app::kWhoYou || e.what == tp::app::kEvPiece; }

PlanState capture(const mpc::Transport& t)
{
    using namespace tp::app;
    PlanState s{};   // (no default initializers: every byte set, so == compares only what is captured)
    s.playing = playing(), s.held = held(), s.locked = locked(), s.loopMode = loopMode(), s.timed = external();
    s.piece = clampU8(piece(), 127);
    s.keyRoot = clampU8(par(P_ROOT), 11);
    s.keyMode = clampU8(par(P_MODE), 127);
    s.now = chord(chordCode());
    s.next = chord(nextChordCode());
    s.barsToNext = clampS8(barsToNextChord());
    s.barsToChange = clampS8(barsToChange());
    s.progPos = clampS8(progPos());
    s.progCount = clampU8(progCount(), 127);
    s.walk = clampU8(walkCharacter(), 127);
    for (int i = 0; i < 8; ++i)
    {
        s.slotState[i] = (uint8_t)slotState(i);
        s.slot[i] = chord(slotChordCode(i));
    }
    s.section = clampU8(section(), 127);
    s.phraseBar = clampU8(phraseBar(), 127);
    s.evolveChance = clampU8(evolveChance(), 127);
    s.densityShift = clampS8(densityShift());
    for (int l = 0; l < kNumLayers && l < 5; ++l)
    {
        if (layerHeard(l)) s.heardMask |= (uint8_t)(1u << l);
        const float a = layerActivity(l) * 127.0f;   // (0..1; not a number: 0)
        s.activity[l] = a >= 127.0f ? 127 : a > 0.0f ? (uint8_t)std::lround(a) : 0;
    }
    s.pos16 = pos16(t);
    return s;
}

} // namespace tp::link

#endif // TP_LINK
