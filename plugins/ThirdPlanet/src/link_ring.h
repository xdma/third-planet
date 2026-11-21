// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: Harmony Link's hand-over from the audio thread (the one producer: watch()) to the link worker (the one
// consumer). Fixed size, no allocation, no locks; both sides wait-free. Only 32-bit atomics, which are lock-free on
// every target (the MPC's 32-bit ARM too).
//
// Two parts behind one interface:
//   - Events: a single-producer, single-consumer queue of N. When it is full, push drops the NEWEST Event (the one
//     being pushed) and counts it in dropped(): the producer never touches the consumer's index, so it cannot drop
//     the oldest. Events come out in push order.
//   - Plan: the latest snapshot only, in a triple buffer. A pushed Plan replaces one not yet popped; the latest Plan
//     is never lost.
// pop() returns the queued Events first, then the pending Plan (spec 5.3: a PLAN follows the event it shows).
#pragma once

#include <atomic>
#include <cstdint>

#include "link_codec.h"

namespace tp::link {

struct Item {
    enum Kind : uint8_t { Plan, Event } kind;
    PlanState plan;   // kind Plan
    LinkEvent ev;     // kind Event
};

template <int N>
class Ring
{
    static_assert(N >= 2 && (N & (N - 1)) == 0, "Ring<N>: N a power of two");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "Ring: 32-bit atomics must be lock-free");

public:
    Ring()
    {
        for (PlanState& p : plans_) p = PlanState{};
        for (LinkEvent& e : events_) e = LinkEvent{};
    }

    // Producer (audio thread). False: an Event dropped, the queue full. A Plan always goes in.
    bool push(const Item& it)
    {
        if (it.kind == Item::Plan)
        {
            plans_[back_] = it.plan;
            back_ = mid_.exchange(back_ | kFresh, std::memory_order_acq_rel) & kIndex;
            return true;
        }
        const uint32_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= (uint32_t)N)
        {
            dropped_.store(dropped_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            return false;
        }
        events_[h & (N - 1)] = it.ev;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Consumer (the worker). False: nothing pending.
    bool pop(Item& out)
    {
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        if (t != head_.load(std::memory_order_acquire))
        {
            out.kind = Item::Event;
            out.ev = events_[t & (N - 1)];
            tail_.store(t + 1, std::memory_order_release);
            return true;
        }
        if (!(mid_.load(std::memory_order_relaxed) & kFresh)) return false;
        front_ = mid_.exchange(front_, std::memory_order_acq_rel) & kIndex;
        out.kind = Item::Plan;
        out.plan = plans_[front_];
        return true;
    }

    int dropped() const { return (int)dropped_.load(std::memory_order_relaxed); }   // Events, since construction

private:
    static constexpr uint32_t kIndex = 3u, kFresh = 4u;

    // Events
    LinkEvent events_[N];
    std::atomic<uint32_t> head_{0};   // written by the producer
    std::atomic<uint32_t> tail_{0};   // written by the consumer
    std::atomic<uint32_t> dropped_{0};
    // Plan: three buffers; each side owns one, mid_ holds the third (and kFresh: it has a Plan not yet popped)
    PlanState plans_[3];
    uint32_t back_ = 0;                // the producer's
    std::atomic<uint32_t> mid_{1};
    uint32_t front_ = 2;               // the consumer's
};

using LinkRing = Ring<128>;

} // namespace tp::link
