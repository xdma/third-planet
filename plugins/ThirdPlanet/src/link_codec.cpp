// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: Harmony Link v1's wire codec (link_codec.h; the format: docs/harmony-link.md).
#include "tp_features.h"

#if TP_LINK

#include "link_codec.h"

namespace tp::link {

namespace {

constexpr uint8_t kHeader[4] = {0xF0, 0x7D, 0x54, 0x50};

uint8_t u7(unsigned v) { return v > 127 ? 127 : (uint8_t)v; }
uint8_t off7(int v, int offset) { v += offset; return v < 0 ? 0 : v > 127 ? 127 : (uint8_t)v; }

int header(uint8_t type, uint8_t seq, uint8_t* out)
{
    for (int i = 0; i < 4; i++) out[i] = kHeader[i];
    out[4] = kVersion;
    out[5] = type;
    out[6] = seq & 0x7F;
    return 7;
}

int put14(int v, uint8_t* out)   // signed: v + 8192 in 0..16383, low 7 bits first
{
    v += 8192;
    v = v < 0 ? 0 : v > 16383 ? 16383 : v;
    out[0] = (uint8_t)(v & 0x7F);
    out[1] = (uint8_t)(v >> 7);
    return 2;
}

int putChord(const Chord& c, uint8_t* out)
{
    out[0] = u7(c.root);
    out[1] = (uint8_t)(c.mask & 0x7F);
    out[2] = (uint8_t)((c.mask >> 7) & 0x1F);
    bool end = false;
    for (int i = 0; i < 8; i++)
    {
        const uint8_t ch = (uint8_t)c.name[i];
        if (ch == 0) end = true;
        out[3 + i] = end ? 0 : ch > 0x7F ? '?' : ch;
    }
    return 11;
}

bool sameChord(const Chord& a, const Chord& b)
{
    if (a.root != b.root || a.mask != b.mask) return false;
    for (int i = 0; i < 8; i++)
        if (a.name[i] != b.name[i]) return false;
    return true;
}

} // namespace

bool PlanState::operator==(const PlanState& o) const
{
    if (playing != o.playing || held != o.held || locked != o.locked || loopMode != o.loopMode || timed != o.timed)
        return false;
    if (piece != o.piece || keyRoot != o.keyRoot || keyMode != o.keyMode) return false;
    if (!sameChord(now, o.now) || !sameChord(next, o.next)) return false;
    if (barsToNext != o.barsToNext || barsToChange != o.barsToChange) return false;
    if (progPos != o.progPos || progCount != o.progCount || walk != o.walk) return false;
    for (int i = 0; i < 8; i++)
        if (slotState[i] != o.slotState[i] || !sameChord(slot[i], o.slot[i])) return false;
    if (section != o.section || phraseBar != o.phraseBar || evolveChance != o.evolveChance ||
        densityShift != o.densityShift)
        return false;
    if (heardMask != o.heardMask) return false;
    for (int i = 0; i < 5; i++)
        if (activity[i] != o.activity[i]) return false;
    return pos16 == o.pos16;
}

int encodePlan(const PlanState& s, uint8_t seq, uint8_t* out)
{
    int n = header(kPlan, seq, out);
    out[n++] = (uint8_t)((s.playing ? 1 : 0) | (s.held ? 2 : 0) | (s.locked ? 4 : 0) | (s.loopMode ? 8 : 0) |
                         (s.timed ? 16 : 0));
    out[n++] = u7(s.piece);
    out[n++] = u7(s.keyRoot);
    out[n++] = u7(s.keyMode);
    n += putChord(s.now, out + n);
    n += putChord(s.next, out + n);
    out[n++] = off7(s.barsToNext, 64);
    out[n++] = off7(s.barsToChange, 64);
    out[n++] = off7(s.progPos, 1);
    out[n++] = u7(s.progCount);
    out[n++] = u7(s.walk);
    for (int i = 0; i < 8; i++)
    {
        out[n++] = u7(s.slotState[i]);
        n += putChord(s.slot[i], out + n);
    }
    out[n++] = u7(s.section);
    out[n++] = u7(s.phraseBar);
    out[n++] = u7(s.evolveChance);
    out[n++] = off7(s.densityShift, 64);
    out[n++] = u7(s.heardMask);
    for (int i = 0; i < 5; i++) out[n++] = u7(s.activity[i]);
    out[n++] = u7(s.pos16);
    out[n++] = 0xF7;
    return n;
}

int encodeEvent(const LinkEvent& e, uint8_t seq, uint8_t* out)
{
    int n = header(kEvent, seq, out);
    out[n++] = u7(e.who);
    out[n++] = u7(e.what);
    out[n++] = off7(e.layer, 1);
    n += put14(e.param, out + n);
    n += put14(e.from, out + n);
    n += put14(e.to, out + n);
    out[n++] = 0xF7;
    return n;
}

int encodeBye(uint8_t seq, uint8_t* out)
{
    int n = header(kBye, seq, out);
    out[n++] = 0xF7;
    return n;
}

bool isLinkSysex(const uint8_t* msg, int len)
{
    if (!msg || len < 4) return false;
    for (int i = 0; i < 4; i++)
        if (msg[i] != kHeader[i]) return false;
    return true;
}

bool decodeCommand(const uint8_t* msg, int len, Command& out)
{
    if (len < 8 || len > kMaxMsg || !isLinkSysex(msg, len)) return false;
    if (msg[len - 1] != 0xF7) return false;
    for (int i = 1; i < len - 1; i++)
        if (msg[i] > 0x7F) return false;
    if (msg[4] != kVersion) return false;
    if (msg[5] == kFade && len == 11)
    {
        out.type = Command::Fade;
        out.level = msg[7];
        out.bars = (uint16_t)(msg[8] | msg[9] << 7);
        out.hold = false;
        return true;
    }
    if (msg[5] == kHold && len == 9 && msg[7] <= 1)
    {
        out.type = Command::Hold;
        out.level = 0;
        out.bars = 0;
        out.hold = msg[7] == 1;
        return true;
    }
    return false;
}

} // namespace tp::link

#endif // TP_LINK
