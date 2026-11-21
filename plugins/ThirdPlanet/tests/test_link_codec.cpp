// SPDX-License-Identifier: GPL-3.0-only
// Harmony Link v1's wire codec (src/link_codec.h) against the golden vectors of docs/harmony-link-vectors.txt, read
// at run time (the file is the single source of truth; docs/harmony-link.md is the format): PLAN, EVENT and BYE
// encode to the vectors' bytes, every payload byte of a random PLAN is 7-bit, FADE and HOLD decode, and malformed
// commands are rejected. Run from the repository root (as tests/all.sh does):
//   c++ -std=c++17 -O1 -DTP_LINK=1 -I plugins/ThirdPlanet/src plugins/ThirdPlanet/tests/test_link_codec.cpp \
//       plugins/ThirdPlanet/src/link_codec.cpp -o plugins/ThirdPlanet/build/tests/test_link_codec
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app_api.h"
#include "link_codec.h"

namespace {

using namespace tp::link;

int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

const char* kVectors = "plugins/ThirdPlanet/docs/harmony-link-vectors.txt";

// The vector NAME's bytes; empty if the file or the line is missing.
std::vector<uint8_t> vec(const char* name)
{
    std::vector<uint8_t> out;
    FILE* f = std::fopen(kVectors, "r");
    if (!f) { std::printf("FAIL: cannot open %s (run from the repository root)\n", kVectors); fails++; return out; }
    char line[2048];
    std::string key = std::string(name) + ":";
    while (std::fgets(line, sizeof line, f))
    {
        if (std::strncmp(line, key.c_str(), key.size()) != 0) continue;
        const char* p = line + key.size();
        unsigned b;
        int n;
        while (std::sscanf(p, " %2x%n", &b, &n) == 1) { out.push_back((uint8_t)b); p += n; }
        break;
    }
    std::fclose(f);
    if (out.empty()) { std::printf("FAIL: no vector %s in %s\n", name, kVectors); fails++; }
    return out;
}

std::string hex(const uint8_t* b, int n)
{
    std::string s;
    char t[4];
    for (int i = 0; i < n; i++) { std::snprintf(t, sizeof t, i ? " %02X" : "%02X", b[i]); s += t; }
    return s;
}

void same(const char* name, const uint8_t* got, int n)
{
    std::vector<uint8_t> want = vec(name);
    if (want.empty()) return;
    bool ok = n == (int)want.size() && std::memcmp(got, want.data(), n) == 0;
    CHECK(ok, "%s:\n  want (%d) %s\n  got  (%d) %s", name, (int)want.size(), hex(want.data(), (int)want.size()).c_str(),
          n, hex(got, n).c_str());
}

Chord chord(int root, std::initializer_list<int> pcs, const char* name)
{
    Chord c;
    c.root = (uint8_t)root;
    for (int pc : pcs) c.mask |= (uint16_t)(1u << pc);
    std::strncpy(c.name, name, sizeof c.name);   // (8 characters: no NUL, as the format allows)
    return c;
}

// PLAN-1's fields, as its comment in the vectors file lists them.
PlanState plan1()
{
    PlanState s{};
    s.playing = true; s.held = true; s.locked = false; s.loopMode = true; s.timed = true;
    s.piece = 7; s.keyRoot = 4; s.keyMode = 3;
    s.now = chord(4, {3, 4, 8, 11}, "Emaj7#11");
    s.next = chord(9, {0, 4, 7, 9, 11}, "Am9");
    s.barsToNext = -2; s.barsToChange = -1;
    s.progPos = -1; s.progCount = 6; s.walk = 2;
    const Chord gap;
    const uint8_t st[8] = {2, 1, 0, 2, 1, 1, 0, 2};
    const Chord sl[8] = {chord(0, {0, 4, 7}, "C"), chord(2, {2, 5, 9}, "Dm"), gap, chord(5, {0, 3, 5, 9}, "F7"),
                         chord(7, {2, 7, 11}, "G"), chord(10, {0, 5, 10}, "Bbsus2"), gap, chord(1, {1, 4, 7}, "C#dim")};
    for (int i = 0; i < 8; i++) { s.slotState[i] = st[i]; s.slot[i] = sl[i]; }
    s.section = 3; s.phraseBar = 6; s.evolveChance = 35; s.densityShift = -5;
    s.heardMask = 0x1D;
    const uint8_t act[5] = {90, 0, 45, 127, 13};
    std::memcpy(s.activity, act, 5);
    s.pos16 = 13;
    return s;
}

uint32_t rng = 12345;
uint8_t rnd() { rng = rng * 1664525u + 1013904223u; return (uint8_t)(rng >> 24); }
void rndChord(Chord& c)
{
    c.root = rnd();
    c.mask = (uint16_t)(rnd() | rnd() << 8);
    for (char& ch : c.name) ch = (char)rnd();
}

void plan_golden()
{
    uint8_t out[kMaxMsg];
    int n = encodePlan(plan1(), 0x21, out);
    CHECK(n == 146, "plan_golden: %d bytes, want 146", n);
    same("PLAN-1", out, n);
}

void plan_all_7bit()
{
    for (int k = 0; k < 1000; k++)
    {
        PlanState s{};
        s.playing = rnd() & 1; s.held = rnd() & 1; s.locked = rnd() & 1; s.loopMode = rnd() & 1; s.timed = rnd() & 1;
        s.piece = rnd(); s.keyRoot = rnd(); s.keyMode = rnd();
        rndChord(s.now); rndChord(s.next);
        s.barsToNext = (int8_t)rnd(); s.barsToChange = (int8_t)rnd();
        s.progPos = (int8_t)rnd(); s.progCount = rnd(); s.walk = rnd();
        for (int i = 0; i < 8; i++) { s.slotState[i] = rnd(); rndChord(s.slot[i]); }
        s.section = rnd(); s.phraseBar = rnd(); s.evolveChance = rnd(); s.densityShift = (int8_t)rnd();
        s.heardMask = rnd();
        for (uint8_t& a : s.activity) a = rnd();
        s.pos16 = rnd();
        uint8_t out[kMaxMsg + 16];
        std::memset(out, 0xAA, sizeof out);
        const uint8_t seq = rnd();
        int n = encodePlan(s, seq, out);
        CHECK(n == 146, "plan_all_7bit: %d bytes", n);
        if (n != 146) return;
        CHECK(out[0] == 0xF0 && out[n - 1] == 0xF7, "plan_all_7bit: not F0 ... F7");
        CHECK(out[n] == 0xAA, "plan_all_7bit: wrote past its length");
        for (int i = 1; i < n - 1; i++)
            if (out[i] > 0x7F) { CHECK(false, "plan_all_7bit: case %d, byte %d is %02X", k, i, out[i]); return; }
    }
}

void plan_equal()
{
    PlanState a = plan1(), b = plan1();
    CHECK(a == b, "plan_equal: equal states differ");
    b.slot[7].name[7] = 'x';
    CHECK(!(a == b), "plan_equal: a slot name's last byte not compared");
    b = a; b.activity[4]++;
    CHECK(!(a == b), "plan_equal: activity not compared");
    b = a; b.timed = false;
    CHECK(!(a == b), "plan_equal: timed not compared");
    b = a; b.next.mask ^= 0x800;
    CHECK(!(a == b), "plan_equal: next's mask not compared");
    b = a; b.pos16++;
    CHECK(!(a == b), "plan_equal: pos16 not compared");
}

void event_golden()
{
    uint8_t out[kMaxMsg];
    int n = encodeEvent({tp::app::kWhoYou, tp::app::kEvNextChord, -1, -1, 0, 0}, 0x22, out);
    CHECK(n == 17, "event_golden: %d bytes, want 17", n);
    same("EVENT-1", out, n);
    n = encodeEvent({tp::app::kWhoEvolve, tp::app::kEvParam, 3, 17, -100, 300}, 0x23, out);
    same("EVENT-2", out, n);
    // out of range: clamped, still 7-bit
    n = encodeEvent({200, 255, 127, 32767, -32768, 9000}, 0x80, out);
    for (int i = 1; i < n - 1; i++) CHECK(out[i] <= 0x7F, "event: byte %d is %02X", i, out[i]);
}

void bye_golden()
{
    uint8_t out[kMaxMsg];
    int n = encodeBye(5, out);
    CHECK(n == 8, "bye_golden: %d bytes, want 8", n);
    same("BYE-1", out, n);
    const uint8_t want[] = {0xF0, 0x7D, 0x54, 0x50, 0x01, 0x03, 0x05, 0xF7};
    CHECK(n == 8 && std::memcmp(out, want, 8) == 0, "bye_golden: %s", hex(out, n).c_str());
}

void decode_fade()
{
    std::vector<uint8_t> m = vec("FADE-1");
    Command c;
    c.type = Command::Hold; c.level = 99; c.bars = 99; c.hold = true;
    CHECK(decodeCommand(m.data(), (int)m.size(), c), "decode_fade: rejected");
    CHECK(c.type == Command::Fade && c.level == 0 && c.bars == 16, "decode_fade: type %d level %d bars %d",
          c.type, c.level, c.bars);
    const uint8_t big[] = {0xF0, 0x7D, 0x54, 0x50, 0x01, 0x10, 0x00, 0x7F, 0x7F, 0x7F, 0xF7};
    CHECK(decodeCommand(big, sizeof big, c) && c.level == 127 && c.bars == 16383, "decode_fade: the largest");
}

void decode_hold()
{
    std::vector<uint8_t> m = vec("HOLD-1");
    Command c;
    CHECK(decodeCommand(m.data(), (int)m.size(), c), "decode_hold: rejected");
    CHECK(c.type == Command::Hold && c.hold, "decode_hold: type %d hold %d", c.type, c.hold);
    const uint8_t off[] = {0xF0, 0x7D, 0x54, 0x50, 0x01, 0x11, 0x00, 0x00, 0xF7};
    CHECK(decodeCommand(off, sizeof off, c) && c.type == Command::Hold && !c.hold, "decode_hold: off");
}

void decode_rejects_garbage()
{
    const uint8_t fade[] = {0xF0, 0x7D, 0x54, 0x50, 0x01, 0x10, 0x00, 0x00, 0x10, 0x00, 0xF7};
    Command c;
    CHECK(decodeCommand(fade, sizeof fade, c), "the base message is rejected");
    for (int n = 0; n < (int)sizeof fade; n++)
        CHECK(!decodeCommand(fade, n, c), "truncated to %d bytes: accepted", n);
    CHECK(!decodeCommand(nullptr, 0, c), "null: accepted");
    auto bad = [&](int at, uint8_t v, const char* what) {
        uint8_t m[sizeof fade];
        std::memcpy(m, fade, sizeof m);
        m[at] = v;
        CHECK(!decodeCommand(m, sizeof m, c), "%s: accepted", what);
    };
    bad(4, 0x02, "version 02");
    bad(8, 0x80, "payload byte 0x80");
    bad(7, 0xFF, "payload byte 0xFF");
    bad(6, 0x80, "seq 0x80");
    bad(1, 0x7E, "manufacturer 7E");
    bad(2, 0x55, "id byte 55");
    bad(0, 0xF1, "no F0");
    bad(5, 0x01, "type 01 (a PLAN)");
    bad(5, 0x02, "type 02 (an EVENT)");
    bad(5, 0x03, "type 03 (a BYE)");
    bad(5, 0x12, "type 12 (unknown)");
    bad(10, 0x00, "no terminating F7");
    const uint8_t hold2[] = {0xF0, 0x7D, 0x54, 0x50, 0x01, 0x11, 0x00, 0x02, 0xF7};
    CHECK(!decodeCommand(hold2, sizeof hold2, c), "HOLD 2: accepted");
    const uint8_t holdLong[] = {0xF0, 0x7D, 0x54, 0x50, 0x01, 0x11, 0x00, 0x01, 0x00, 0xF7};
    CHECK(!decodeCommand(holdLong, sizeof holdLong, c), "HOLD with an extra byte: accepted");
    std::vector<uint8_t> plan = vec("PLAN-1");
    CHECK(!decodeCommand(plan.data(), (int)plan.size(), c), "a PLAN: accepted");
    uint8_t huge[kMaxMsg + 1];
    std::memset(huge, 0, sizeof huge);
    std::memcpy(huge, fade, 10);
    huge[kMaxMsg] = 0xF7;
    CHECK(!decodeCommand(huge, sizeof huge, c), "over kMaxMsg: accepted");
    // a length past kMaxMsg is refused before any byte is read (under ASan, a read here would be reported)
    CHECK(!decodeCommand(fade, 100000, c), "len 100000: accepted");
}

void link_sysex()
{
    const uint8_t a[] = {0xF0, 0x7D, 0x54, 0x50};
    const uint8_t b[] = {0xF0, 0x7D, 0x54, 0x51, 0x01};
    CHECK(isLinkSysex(a, 4), "isLinkSysex: the prefix");
    CHECK(!isLinkSysex(a, 3), "isLinkSysex: 3 bytes");
    CHECK(!isLinkSysex(b, 5), "isLinkSysex: 54 51");
    CHECK(!isLinkSysex(nullptr, 0), "isLinkSysex: null");
    std::vector<uint8_t> p = vec("PLAN-1");
    CHECK(isLinkSysex(p.data(), (int)p.size()), "isLinkSysex: PLAN-1");
}

} // namespace

int main()
{
    plan_golden();
    plan_all_7bit();
    plan_equal();
    event_golden();
    bye_golden();
    decode_fade();
    decode_hold();
    decode_rejects_garbage();
    link_sysex();
    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("test_link_codec: all passed\n");
    return 0;
}
