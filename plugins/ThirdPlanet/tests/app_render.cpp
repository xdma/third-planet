// SPDX-License-Identifier: GPL-3.0-only
// Third Planet's FM-1 app (app_unit.cpp) driven by an upstream host-simulator script, written as upstream's
// amb_host writes it, so tests/app_null.sh can compare the two WAVs byte for byte. It runs the script commands
// that drive the device (wait, press, release, tap, key, tapkey, strum, turn, spin, master, midi, wav, wavstop)
// with the same timings as amb_host.c, and skips the checks (expect, said, shot, leds, peakreset ...).
//   app_render SCRIPT OUTDIR
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app_api.h"

namespace {

const char* const kBtn[] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
const char* const kEnc[] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};
const unsigned char kWhite[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
const unsigned char kBlack[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

uint32_t btn = 0, keys = 0;
FILE* wav = nullptr;
uint32_t wavFrames = 0;

int findName(const char* const* t, int n, const char* s)
{
    for (int i = 0; i < n; ++i)
        if (!std::strcmp(t[i], s)) return i;
    return -1;
}
int keyOf(const char* s)
{
    if (s[0] == 'w') return kWhite[std::atoi(s + 1) & 15];
    if (s[0] == 'b') return kBlack[std::atoi(s + 1) % 11];
    return std::atoi(s);
}
void runMs(uint32_t ms)
{
    static int32_t blk[512];
    for (uint32_t i = 0; i < ms; ++i)
    {
        tp::app::setButtons(btn);
        tp::app::setKeys(keys);
        if (tp::app::stepMs(blk) && wav)
        {
            for (int k = 0; k < 256; ++k)
            {
                float l = (float)blk[2 * k] / 8388608.0f, r = (float)blk[2 * k + 1] / 8388608.0f;
                int16_t s[2] = {(int16_t)(l * 32767.0f), (int16_t)(r * 32767.0f)};
                std::fwrite(s, 2, 2, wav);
                wavFrames++;
            }
        }
    }
}
void put32(unsigned char* p, uint32_t v) { p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24); }
void wavClose()
{
    if (!wav) return;
    unsigned char h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0};
    put32(h + 4, 36 + wavFrames * 4);
    put32(h + 24, 44100), put32(h + 28, 44100 * 4);
    h[32] = 4, h[34] = 16;
    std::memcpy(h + 36, "data", 4);
    put32(h + 40, wavFrames * 4);
    std::fseek(wav, 0, SEEK_SET);
    std::fwrite(h, 1, 44, wav);
    std::fclose(wav);
    wav = nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    FILE* sc = std::fopen(argv[1], "r");
    if (!sc) return 2;
    tp::app::boot();
    char line[512], out[600];
    while (std::fgets(line, sizeof line, sc))
    {
        char cmd[32] = {0}, a[256] = {0}, b[64] = {0}, c[64] = {0};
        if (line[0] == '#' || std::sscanf(line, "%31s %255s %63s %63s", cmd, a, b, c) < 1) continue;
        if (!std::strcmp(cmd, "wait")) runMs((uint32_t)std::atoi(a));
        else if (!std::strcmp(cmd, "press") || !std::strcmp(cmd, "release") || !std::strcmp(cmd, "tap"))
        {
            int i = findName(kBtn, 14, a);
            if (i < 0) return 2;
            if (cmd[0] != 'r') btn |= 1u << i;
            if (cmd[0] == 't') runMs(40);
            if (cmd[0] != 'p') btn &= ~(1u << i);
            runMs(cmd[0] == 't' ? 40 : 2);
        }
        else if (!std::strcmp(cmd, "key") || !std::strcmp(cmd, "tapkey"))
        {
            int k = keyOf(a);
            if (cmd[0] == 't') { keys |= 1u << k; runMs(60); keys &= ~(1u << k); runMs(30); }
            else { if (!std::strcmp(b, "down")) keys |= 1u << k; else keys &= ~(1u << k); runMs(2); }
        }
        else if (!std::strcmp(cmd, "strum"))
        {
            int x = std::atoi(a), y = std::atoi(b), gap = c[0] ? std::atoi(c) : 25, s = y >= x ? 1 : -1;
            for (;; x += s)
            {
                keys |= 1u << kWhite[x & 15];
                runMs((uint32_t)gap);
                keys &= ~(1u << kWhite[x & 15]);
                if (x == y) break;
            }
            runMs(2);
        }
        else if (!std::strcmp(cmd, "turn"))
        {
            int e = findName(kEnc, 7, a), n = std::atoi(b), s = n > 0 ? 1 : -1;
            uint32_t gap = c[0] ? (uint32_t)std::atoi(c) : 80u;
            if (e < 0) return 2;
            for (; n; n -= s) { tp::app::turn(e, s); runMs(gap); }
        }
        else if (!std::strcmp(cmd, "spin"))
        {
            int e = findName(kEnc, 7, a);
            if (e < 0) return 2;
            tp::app::turn(e, std::atoi(b));
            tp::app::setAccelOff(true);
            runMs(1);
            tp::app::setAccelOff(false);
        }
        else if (!std::strcmp(cmd, "master")) tp::app::setMaster((uint32_t)std::atoi(a));
        else if (!std::strcmp(cmd, "midi"))
        {
            uint32_t s = (uint32_t)std::strtoul(a, 0, 16), d1 = (uint32_t)std::strtoul(b, 0, 16), d2 = (uint32_t)std::strtoul(c, 0, 16);
            uint32_t cin = s >= 0xF0 ? 0x0F : s >> 4;
            tp::app::midiIn(cin | s << 8 | d1 << 16 | d2 << 24);
        }
        else if (!std::strcmp(cmd, "wav"))
        {
            std::snprintf(out, sizeof out, "%s/%s", argv[2], a);
            wav = std::fopen(out, "wb");
            static const unsigned char h[44] = {0};
            if (wav) std::fwrite(h, 1, 44, wav);
            wavFrames = 0;
        }
        else if (!std::strcmp(cmd, "wavstop")) wavClose();
    }
    wavClose();
    return 0;
}
