// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: the NACHTFAHRT engine as the plugin sees it.
//
// The engine (src/nachtfahrt/dsp/, compiled by engine_unit.cpp) and the FM-1 app around it
// (app_unit.cpp) are one set of C globals, so there is one of them per process: EngineLease hands
// them to one plugin instance at a time.
#pragma once

#include <atomic>
#include <cstdint>

extern "C" {
#include "nachtfahrt/dsp/amb.h"
}

namespace tp {

constexpr int kChunk = 256;   // frames per en_render call, as on the FM-1

// The engine's output is the FM-1's 24-bit-in-32 sample (upstream writes buf >> 8 as 16 bit).
// Full scale is therefore 2^23, and the plugin keeps every bit instead of truncating to 16 bit.
constexpr float kOutScale = 1.0f / 8388608.0f;

// One owner of the engine per process. claim() succeeds for the first instance to ask, and for
// any instance after the owner has released it; `fresh` tells the caller it has just taken it over
// (the engine holds someone else's state, or none).
class EngineLease
{
public:
    bool claim(const void* who, bool& fresh)
    {
        const void* expected = nullptr;
        if (owner().compare_exchange_strong(expected, who, std::memory_order_acq_rel))
        {
            fresh = true;
            return true;
        }
        fresh = false;
        return expected == who;
    }
    void release(const void* who)
    {
        const void* expected = who;
        owner().compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }
    bool owns(const void* who) const { return owner().load(std::memory_order_acquire) == who; }

private:
    static std::atomic<const void*>& owner()
    {
        static std::atomic<const void*> o{nullptr};
        return o;
    }
};

} // namespace tp
