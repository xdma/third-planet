// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: Harmony Link's worker thread (link_worker.h).
#include "tp_features.h"

#if TP_LINK

#include "link_worker.h"

#include <chrono>
#include <cstring>

namespace tp::link {

namespace {

struct SystemClock : Clock
{
    double nowMs() override
    {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }
    void sleepMs(int ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};

constexpr char kTarget[] = "f_midi";   // the USB MIDI function; its port 0 is the computer's "MPC Live III Port 1"
constexpr int kTargetPort = 0;
constexpr int kMaxTake = 4 * 128;      // ring items a poll takes at most (the audio thread cannot hold the worker)
constexpr int kMaxInput = 256;         // input events a poll reads at most

} // namespace

Clock& systemClock()
{
    static SystemClock clock;
    return clock;
}

Worker::Worker(LinkRing& ring, SeqApi api, Clock& clock) : ring_(ring), api_(api), clock_(clock), loaded_(true) {}
Worker::Worker(LinkRing& ring, Loader loader, Clock& clock) : ring_(ring), loader_(loader), clock_(clock) {}
Worker::~Worker() { stop(); }

void Worker::start()
{
    if (thread_.joinable()) return;
    quit_.store(false, std::memory_order_release);
    thread_ = std::thread([this] { run(); });
}

void Worker::stop()
{
    if (!thread_.joinable()) return;
    quit_.store(true, std::memory_order_release);
    thread_.join();
}

void Worker::run()
{
    while (!quit_.load(std::memory_order_acquire))
    {
        if (!enabled_.load(std::memory_order_acquire))
        {
            if (open_) closeLink();
            clock_.sleepMs(kIdleMs);
            continue;
        }
        if (!open_) openLink();
        poll();
        clock_.sleepMs(api_.ok ? kPollMs : kIdleMs);
    }
    if (open_) closeLink();
}

// Link On. What the ring held is from before (the audio thread pushes nothing while the link is off, so these are
// items the last session did not take): dropped, and a fresh Plan asked for, after the drain so that it is not
// dropped too.
void Worker::openLink()
{
    if (!loaded_)
    {
        if (loader_) api_ = loader_();
        loaded_ = true;
    }
    Item it;
    for (int n = 0; n < kMaxTake && ring_.pop(it); ++n) {}
    havePlan_ = planDue_ = false;
    inOn_ = false;
    nextTry_ = clock_.nowMs();
    connectedAt_ = planAt_ = -1e18;
    open_ = true;
    state.store(api_.ok ? Searching : NoAlsa, std::memory_order_relaxed);
    wantPlan_.store(true, std::memory_order_release);
}

// Link Off, or the thread ending: BYE if anyone can hear it, then the client goes.
void Worker::closeLink()
{
    const double now = clock_.nowMs();
    if (target_ >= 0)
    {
        uint8_t msg[kMaxMsg];
        send(msg, encodeBye(nextSeq(), msg), now);
    }
    closeSeq(now);
    open_ = false;
    state.store(Off, std::memory_order_relaxed);
}

void Worker::poll()
{
    const double now = clock_.nowMs();
    if (api_.ok)
    {
        if (target_ < 0)
        {
            if (now >= nextTry_) connect(now);
        }
        else if (now >= nextCheck_)   // (f_midi gone takes the subscription with it, and sends still succeed)
        {
            nextCheck_ = now + kRetryMs;
            if (api_.findClient(kTarget) != target_) closeSeq(now);
        }
    }

    Item it;
    uint8_t msg[kMaxMsg];
    uint32_t taken = 0;
    for (int n = 0; n < kMaxTake && ring_.pop(it); ++n, ++taken)
    {
        if (it.kind == Item::Plan)
        {
            plan_ = it.plan;
            havePlan_ = planDue_ = true;
        }
        else if (target_ >= 0)   // (an Event with no one to hear it is dropped: it is news, not state)
            send(msg, encodeEvent(it.ev, nextSeq(), msg), now);
    }
    if (taken) takenCount.store(takenCount.load(std::memory_order_relaxed) + taken, std::memory_order_relaxed);

    if (target_ >= 0 && havePlan_ &&
        ((planDue_ && now - planAt_ >= kPlanMinMs) || now - planAt_ >= kHeartbeatMs))
    {
        if (send(msg, encodePlan(plan_, nextSeq(), msg), now) == Sent::Ok)
        {
            planDue_ = false;
            planAt_ = now;
        }
        // (busy: again on the next poll; failed: after the reconnect)
    }
    if (target_ >= 0) readInput();
}

// The client, its two ports and the connection to f_midi. Whatever fails, the next try is kRetryMs away.
void Worker::connect(double now)
{
    nextTry_ = now + kRetryMs;
    if (!h_)
    {
        void* h = nullptr;
        if (api_.open(&h, "default", kSeqOpenDuplex, 0) < 0 || !h) return;
        h_ = h;
        api_.setName(h_, "Third Planet");
        api_.nonblock(h_, 1);
        const unsigned type = kSeqTypeMidiGeneric | kSeqTypeApplication;
        // READ without SUBS_READ: only this client can connect the port, so the MPC cannot subscribe to it and
        // nothing comes back into the plugin's own track.
        outPort_ = api_.createPort(h_, "Harmony Link", kSeqCapRead, type);
        inPort_ = api_.createPort(h_, "Harmony Link In", kSeqCapWrite | kSeqCapSubsWrite, type);
        if (outPort_ < 0 || inPort_ < 0)
        {
            closeSeq(now);
            return;
        }
    }
    const int id = api_.findClient(kTarget);
    if (id < 0) return;   // (USB MIDI is not there: the client stays, the search goes on)
    if (api_.connectTo(h_, outPort_, id, kTargetPort) < 0)
    {
        closeSeq(now);
        return;
    }
    api_.connectFrom(h_, inPort_, id, kTargetPort);   // (no commands without it; the plan still goes out)
    target_ = id;
    connectedAt_ = now;
    nextCheck_ = now + kRetryMs;
    planAt_ = -1e18;        // the newest Plan first, at once
    planDue_ = havePlan_;
    inOn_ = false;
    state.store(Connected, std::memory_order_relaxed);
}

// The client goes (closing it drops its ports and connections). The link stays on: a new client, no sooner than
// kRetryMs after the last connection, so a connection that keeps failing is not remade every poll.
void Worker::closeSeq(double now)
{
    if (h_) api_.close(h_);
    h_ = nullptr;
    outPort_ = inPort_ = -1;
    if (target_ >= 0)
    {
        const double soonest = connectedAt_ + kRetryMs;
        nextTry_ = soonest > now ? soonest : now;
        planDue_ = havePlan_;
    }
    target_ = -1;
    if (open_ && api_.ok) state.store(Searching, std::memory_order_relaxed);
}

// One SysEx, direct to the port's subscribers. Failed: the client is closed (see closeSeq).
Worker::Sent Worker::send(const uint8_t* msg, int len, double now)
{
    if (!h_ || target_ < 0) return Sent::Failed;
    SeqEvent ev;
    std::memset(&ev, 0, sizeof ev);
    ev.type = kSeqEvSysex;
    ev.flags = kSeqLengthVariable;
    ev.queue = kSeqQueueDirect;
    ev.source.port = (unsigned char)outPort_;
    ev.dest.client = kSeqAddrSubscribers;
    ev.dest.port = kSeqAddrUnknown;
    ev.data.ext.len = (unsigned)len;
    ev.data.ext.ptr = const_cast<uint8_t*>(msg);
    const int r = api_.outputDirect(h_, &ev);
    if (r >= 0)
    {
        sentCount.store(sentCount.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        return Sent::Ok;
    }
    if (r == -kSeqEAgain) return Sent::Busy;
    sendErrors.store(sendErrors.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    closeSeq(now);
    return Sent::Failed;
}

// What the computer sent to "MPC Live III Port 1": its clock and anything else (ignored), and SysEx.
void Worker::readInput()
{
    for (int n = 0; n < kMaxInput && h_; ++n)
    {
        void* p = nullptr;
        if (api_.input(h_, &p) < 0 || !p) return;   // (-EAGAIN: nothing more; an overrun: the next poll goes on)
        SeqEvent ev;
        std::memcpy(&ev, p, sizeof ev);
        if (ev.type != kSeqEvSysex || (ev.flags & kSeqLengthMask) != kSeqLengthVariable) continue;
        const void* data = ev.data.ext.ptr;
        if (data) feed(static_cast<const uint8_t*>(data), ev.data.ext.len);
    }
}

// SysEx may come in pieces: F0 to F7 put together, kMaxMsg at most (a longer one is dropped whole), then decoded.
// Only a FADE or a HOLD of v1 does anything; decodeCommand() refuses everything else, a message with a byte over 7F
// in it (one cut off by something else) included. A new F0 starts over.
void Worker::feed(const uint8_t* bytes, unsigned len)
{
    for (unsigned i = 0; i < len; ++i)
    {
        const uint8_t b = bytes[i];
        if (b == 0xF0)
        {
            inOn_ = true;
            inOver_ = false;
            inLen_ = 0;
        }
        if (!inOn_) continue;
        if (inLen_ < kMaxMsg) in_[inLen_++] = b;
        else inOver_ = true;
        if (b == 0xF7)
        {
            inOn_ = false;
            Command c;
            if (inOver_ || !decodeCommand(in_, inLen_, c)) continue;
            if (c.type == Command::Fade)
            {
                fadeLevel.store(c.level, std::memory_order_relaxed);
                fadeBars.store(c.bars, std::memory_order_relaxed);
                fadeSeq.fetch_add(1, std::memory_order_release);   // (last: see link_worker.h)
            }
            else if (c.type == Command::Hold)
                holdCmd.store(c.hold ? 1 : 0, std::memory_order_release);
        }
    }
}

} // namespace tp::link

#endif // TP_LINK
