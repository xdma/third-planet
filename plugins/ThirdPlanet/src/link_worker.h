// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: Harmony Link's worker (docs/superpowers/specs/2026-11-21-harmony-link-design.md, 5.2 and 5.6): its
// own thread, the one consumer of the audio thread's ring, and the only code that touches ALSA.
//
// While the link is on it:
//   - opens the sequencer client "Third Planet" with the port "Harmony Link" (readable by its owner only: nothing can
//     subscribe to it, so nothing echoes) and a port to listen on, and connects both to the USB MIDI function
//     ("f_midi" port 0, the computer's "MPC Live III Port 1"). No f_midi: it looks again every 2 s;
//   - every 2 ms takes what the ring has: Events go out at once, in order; of the Plans only the newest, at most 20 a
//     second; with nothing new the last Plan goes out again every 2 s (the heartbeat), while the audio thread runs
//     (tick(): no block for 500 ms, no heartbeat);
//   - a send error, or f_midi gone or changed (looked up every 2 s), ends the client; a new one is made and
//     connected, no sooner than 2 s after the last connection; then the newest Plan goes out first;
//   - reads the computer's commands (FADE, HOLD), which may come in pieces, into the atomics below;
//   - sends BYE and closes when the link goes off.
// Opening the link drops whatever the ring still held from before and asks the audio thread for a fresh Plan
// (takeWantPlan), so a Plan always follows Link On.
//
// The audio thread's whole contact: the ring's push, setEnabled, takeWantPlan, tick and the command atomics. All wait-free
// (lock-free 32-bit atomics); it never starts, stops, joins or waits for the thread.
// (Defined in link_worker.cpp only with TP_LINK=1.)
#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "link_alsa.h"
#include "link_codec.h"
#include "link_ring.h"

namespace tp::link {

struct Clock
{
    virtual double nowMs() = 0;
    virtual void sleepMs(int) = 0;
    virtual ~Clock() = default;
};
Clock& systemClock();   // the steady clock and a real sleep

class Worker
{
public:
    using Loader = SeqApi (*)();
    Worker(LinkRing&, SeqApi, Clock&);
    Worker(LinkRing&, Loader, Clock&);   // the API loaded by the thread itself, when the link first opens
    ~Worker();                           // stop()
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    // The thread. Not for the audio thread: start() makes it, stop() has it send BYE and close, and joins it.
    // Both from one thread at a time; start() while running and stop() while stopped do nothing. If the system gives
    // no thread (std::thread throws), start() returns all the same: there is no worker, the link does nothing.
    void start();
    void stop();

    // Link On or Off (any thread, wait-free; on at first). Off, the thread sends BYE, closes and idles.
    void setEnabled(bool on) { enabled_.store(on, std::memory_order_release); }
    // The audio thread, once a block with Link On: true once after the link opened; push a Plan then, changed or not.
    bool takeWantPlan()
    {
        return wantPlan_.load(std::memory_order_relaxed) && wantPlan_.exchange(false, std::memory_order_acq_rel);
    }

    // Commands for the audio thread, written by the worker.
    // FADE: fadeLevel (0..127) and fadeBars (0..16383) are written first, then fadeSeq goes up by one: a reader that
    // sees fadeSeq change reads level and bars after it (and again if fadeSeq moved meanwhile).
    std::atomic<uint32_t> fadeSeq{0}, fadeLevel{127}, fadeBars{0};
    std::atomic<int> holdCmd{-1};   // -1 none, 0 off, 1 on; the audio thread exchanges it with -1

    // What the thread is doing (to look at; nothing depends on it).
    enum State : uint32_t { Off, NoAlsa, Searching, Connected };
    std::atomic<uint32_t> state{Off};
    std::atomic<uint32_t> sentCount{0}, sendErrors{0}, takenCount{0};   // messages sent, failed sends, ring items taken
    bool running() const { return thread_.joinable(); }   // (false also when the system gave no thread: start())
    bool failStart = false;   // (tests: start() as if the system had no thread to give)

    // The audio thread, once a block: it is running. No block for kStaleMs (the MPC stopped calling the plugin) and
    // the heartbeat stops, so the computer sees the plan go stale; it resumes with the blocks.
    void tick() { blocks.store(blocks.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed); }
    std::atomic<uint32_t> blocks{0};

    // kPollMs: the poll while the link is on with libasound (connected or searching); kNoAlsaMs: with none, only the
    // ring's drain; kIdleMs: Link Off (quit_ is looked at every pass, so stop() joins within about this).
    static constexpr int kPollMs = 2, kNoAlsaMs = 20, kIdleMs = 100, kPlanMinMs = 50, kHeartbeatMs = 2000,
                         kRetryMs = 2000, kStaleMs = 500;

private:
    enum class Sent { Ok, Busy, Failed };

    void run();
    void openLink();
    void closeLink();
    void poll();
    void connect(double now);
    void closeSeq(double now);
    Sent send(const uint8_t* msg, int len, double now);
    void readInput();
    void feed(const uint8_t* bytes, unsigned len);
    uint8_t nextSeq() { const uint8_t s = seq_; seq_ = (uint8_t)((seq_ + 1) & 0x7F); return s; }

    LinkRing& ring_;
    SeqApi api_;
    Loader loader_ = nullptr;
    Clock& clock_;
    std::thread thread_;
    std::atomic<bool> quit_{false}, enabled_{true}, wantPlan_{false};

    // the thread's own
    bool loaded_ = false;      // api_ is final
    bool open_ = false;        // the link is on (whether or not there is a client or a connection)
    void* h_ = nullptr;        // the sequencer client (snd_seq_t*)
    int outPort_ = -1, inPort_ = -1;
    int target_ = -1;          // f_midi's client while connected; -1: not connected
    double nextTry_ = 0.0, nextCheck_ = 0.0, connectedAt_ = -1e18, planAt_ = -1e18;
    uint32_t blocksSeen_ = 0;  // the audio thread's block count last seen, and when it moved
    double blocksAt_ = -1e18;
    PlanState plan_{};         // the newest Plan taken since the link opened
    bool havePlan_ = false, planDue_ = false;
    uint8_t seq_ = 0;          // the running 7-bit counter of the messages
    uint8_t in_[kMaxMsg];      // a command being put together, F0 to F7
    int inLen_ = 0;
    bool inOn_ = false, inOver_ = false;
};

} // namespace tp::link
