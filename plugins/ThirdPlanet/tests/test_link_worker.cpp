// SPDX-License-Identifier: GPL-3.0-only
// Harmony Link's worker (src/link_worker.h; docs/superpowers/specs/2026-11-21-harmony-link-design.md, 5.2 and 5.6)
// against a fake ALSA sequencer (a SeqApi that records what is sent and serves queued input) and a fake clock the
// test steps one poll at a time, so every count below is exact:
//   - a Plan pushed goes out within one poll; Plans are coalesced to 20 a second, the last one pushed always sent;
//     with nothing pushed the last Plan goes out again every 2 s; Events go out at once, in order;
//   - stop() and Link Off send a BYE last and close; the out port can be read by its owner only (no echo);
//   - the USB MIDI function absent: nothing is sent, the worker looks for it every 2 s, and connects when it is
//     back; a send error, or the function gone without one, means a new client and a new connection;
//   - FADE and HOLD arrive in pieces and reach the atomics the audio thread reads; anything else is ignored;
//   - items left in the ring from before are not sent, and a fresh Plan is asked for;
//   - 200 starts and stops, and 400 flips of Link on a running worker, the audio thread pushing all the while: no
//     hang, every open closed, a PLAN after the last On; with no libasound the ring is still drained.
// TP_LINK=1 only. Built alone, under ASan and UBSan (tests/all.sh), and under TSan by hand:
//   c++ -std=c++17 -O1 -g -fsanitize=address,undefined -DTP_LINK=1 -I plugins/ThirdPlanet/src \
//       plugins/ThirdPlanet/tests/test_link_worker.cpp plugins/ThirdPlanet/src/link_worker.cpp \
//       plugins/ThirdPlanet/src/link_alsa.cpp plugins/ThirdPlanet/src/link_codec.cpp \
//       -o plugins/ThirdPlanet/build/tests/test_link_worker
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "tp_features.h"

#if !TP_LINK
#error "test_link_worker needs TP_LINK=1"
#endif

#include "link_alsa.h"
#include "link_codec.h"
#include "link_ring.h"
#include "link_worker.h"

namespace {

using namespace tp::link;
using Bytes = std::vector<uint8_t>;

int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

// ---- the fake sequencer (SeqApi has no context pointer: one global, under a mutex) ----
struct Port { std::string name; unsigned caps, type; };
struct Conn { int port, client, cport; };
struct Fake
{
    std::mutex m;
    std::vector<Bytes> sent;
    std::vector<int> sentPort;        // the source port of each
    std::deque<Bytes> input;          // SysEx chunks waiting, one event each
    std::deque<int> inputType;        // ... and each one's event type
    std::vector<Port> ports;
    std::vector<Conn> to, from;
    int opens = 0, closes = 0, finds = 0;
    int client = 24;                  // findClient("f_midi")
    int sendErr = 0;                  // not 0: the next send returns it, once
    int sendErrAlways = 0;            // not 0: every send returns it
    bool open = false;
    bool nonblocking = false;
    int sendsWhileClosed = 0;
    std::string name;
    SeqEvent inEv;
    Bytes inData;
    void reset()
    {
        std::lock_guard<std::mutex> l(m);
        sent.clear(), sentPort.clear(), input.clear(), inputType.clear(), ports.clear(), to.clear(), from.clear();
        opens = closes = finds = 0, client = 24, sendErr = sendErrAlways = 0, open = nonblocking = false;
        sendsWhileClosed = 0, name.clear();
    }
} fake;

int fOpen(void** h, const char*, int, int)
{
    std::lock_guard<std::mutex> l(fake.m);
    fake.opens++, fake.open = true, fake.ports.clear();
    *h = &fake;
    return 0;
}
int fClose(void*)
{
    std::lock_guard<std::mutex> l(fake.m);
    fake.closes++, fake.open = false;
    return 0;
}
int fSetName(void*, const char* n)
{
    std::lock_guard<std::mutex> l(fake.m);
    fake.name = n;
    return 0;
}
int fCreatePort(void*, const char* n, unsigned caps, unsigned type)
{
    std::lock_guard<std::mutex> l(fake.m);
    fake.ports.push_back(Port{n, caps, type});
    return (int)fake.ports.size() - 1;
}
int fOutputDirect(void*, void* evp)
{
    std::lock_guard<std::mutex> l(fake.m);
    if (!fake.open) { fake.sendsWhileClosed++; return -9; }
    if (fake.sendErrAlways) return fake.sendErrAlways;
    if (fake.sendErr) { const int e = fake.sendErr; fake.sendErr = 0; return e; }
    SeqEvent ev;
    std::memcpy(&ev, evp, sizeof ev);
    if (ev.type != kSeqEvSysex || !(ev.flags & kSeqLengthVariable) || ev.queue != kSeqQueueDirect ||
        ev.dest.client != kSeqAddrSubscribers)
        return -22;
    const uint8_t* p = (const uint8_t*)ev.data.ext.ptr;
    fake.sent.emplace_back(p, p + ev.data.ext.len);
    fake.sentPort.push_back(ev.source.port);
    return (int)ev.data.ext.len;
}
int fConnectTo(void*, int port, int client, int cport)
{
    std::lock_guard<std::mutex> l(fake.m);
    fake.to.push_back(Conn{port, client, cport});
    return 0;
}
int fConnectFrom(void*, int port, int client, int cport)
{
    std::lock_guard<std::mutex> l(fake.m);
    fake.from.push_back(Conn{port, client, cport});
    return 0;
}
int fNonblock(void*, int on)
{
    std::lock_guard<std::mutex> l(fake.m);
    fake.nonblocking = on != 0;
    return 0;
}
int fInput(void*, void** evp)
{
    std::lock_guard<std::mutex> l(fake.m);
    if (fake.input.empty()) return -11;   // (-EAGAIN: nothing waiting)
    fake.inData = fake.input.front();
    std::memset(&fake.inEv, 0, sizeof fake.inEv);
    fake.inEv.type = (unsigned char)fake.inputType.front();
    fake.input.pop_front(), fake.inputType.pop_front();
    if (fake.inEv.type == kSeqEvSysex)
    {
        fake.inEv.flags = kSeqLengthVariable;
        fake.inEv.data.ext.len = (unsigned)fake.inData.size();
        fake.inEv.data.ext.ptr = fake.inData.data();
    }
    *evp = &fake.inEv;
    return (int)fake.input.size();
}
int fFindClient(const char* n)
{
    std::lock_guard<std::mutex> l(fake.m);
    fake.finds++;
    return std::strcmp(n, "f_midi") == 0 ? fake.client : -1;
}
SeqApi fakeApi()
{
    SeqApi a;
    a.ok = true;
    a.open = fOpen, a.close = fClose, a.setName = fSetName, a.createPort = fCreatePort;
    a.outputDirect = fOutputDirect, a.connectTo = fConnectTo, a.connectFrom = fConnectFrom;
    a.nonblock = fNonblock, a.input = fInput, a.findClient = fFindClient;
    return a;
}
template <class F> auto locked(F f) { std::lock_guard<std::mutex> l(fake.m); return f(); }
std::vector<Bytes> sent() { return locked([] { return fake.sent; }); }
void queueInput(const Bytes& b, int type = kSeqEvSysex) { locked([&] { fake.input.push_back(b); fake.inputType.push_back(type); return 0; }); }

// ---- the fake clock: the worker's sleep is a gate, opened one poll at a time ----
struct StepClock : Clock
{
    std::mutex m;
    std::condition_variable cv;
    long long t = 0;          // ms
    long sleeps = 0, allowed = 0;
    bool free = false;
    std::atomic<uint32_t>* blocks = nullptr;   // the worker's block count: the audio thread's blocks, one a pass
    std::atomic<bool> ticking{true};           // ... while true (false: the audio thread stopped)
    double nowMs() override { std::lock_guard<std::mutex> l(m); return (double)t; }
    void sleepMs(int ms) override
    {
        std::unique_lock<std::mutex> l(m);
        ++sleeps;
        cv.notify_all();
        cv.wait(l, [&] { return free || allowed >= sleeps; });
        t += ms;
        if (blocks && ticking.load()) blocks->fetch_add(1);
    }
    // (test) the worker is asleep after its first pass
    void settle() { std::unique_lock<std::mutex> l(m); cv.wait(l, [&] { return sleeps > allowed; }); }
    // (test) n more passes of the worker's loop, each after its sleep; back when it sleeps again
    void step(int n = 1)
    {
        std::unique_lock<std::mutex> l(m);
        allowed += n;
        cv.notify_all();
        cv.wait(l, [&] { return sleeps > allowed; });
    }
    void ms(int n) { step(n / 2); }   // (n ms of connected polls, 2 ms each)
    void release() { std::lock_guard<std::mutex> l(m); free = true; cv.notify_all(); }
};
// (the stress test's: never waits)
struct FreeClock : Clock
{
    std::atomic<long long> t{0};
    double nowMs() override { return (double)t.load(); }
    void sleepMs(int ms) override { t.fetch_add(ms); std::this_thread::yield(); }
};

struct Rig
{
    LinkRing ring;
    StepClock clock;
    std::unique_ptr<Worker> w;
    explicit Rig(bool ok = true)
    {
        fake.reset();
        SeqApi a = fakeApi();
        a.ok = ok;
        w = std::make_unique<Worker>(ring, a, clock);
        clock.blocks = &w->blocks;
    }
    void start() { w->start(); clock.settle(); }
    void stop() { clock.release(); w->stop(); }
    ~Rig() { stop(); }
    void plan(const PlanState& p) { Item it{}; it.kind = Item::Plan; it.plan = p; ring.push(it); }
    void event(int to) { Item it{}; it.kind = Item::Event; it.ev = LinkEvent{0, 3, -1, 0, 0, (int16_t)to}; ring.push(it); }
};

PlanState planOf(int n)
{
    PlanState p{};
    p.playing = true;
    p.piece = (uint8_t)(n % 8);
    p.keyRoot = (uint8_t)(n % 12);
    p.phraseBar = (uint8_t)(n % 8);
    p.evolveChance = (uint8_t)(n % 101);
    p.pos16 = (uint8_t)(n % 16);
    p.activity[0] = (uint8_t)(n % 128);
    return p;
}
int typeOf(const Bytes& m) { return m.size() >= 8 && isLinkSysex(m.data(), (int)m.size()) ? m[5] : -1; }
int countType(const std::vector<Bytes>& v, int type)
{
    int n = 0;
    for (const Bytes& m : v)
        n += typeOf(m) == type;
    return n;
}
// m is the PLAN of p (whatever its seq)
bool isPlan(const Bytes& m, const PlanState& p)
{
    uint8_t buf[kMaxMsg];
    if ((int)m.size() != kPlanLen || typeOf(m) != kPlan) return false;
    return encodePlan(p, m[6], buf) == kPlanLen && std::memcmp(buf, m.data(), kPlanLen) == 0;
}

void sends_plan_within_poll()
{
    Rig r;
    r.start();
    CHECK(sent().empty(), "sends_plan_within_poll: sent with nothing pushed");
    r.plan(planOf(3));
    r.clock.step();
    const auto s = sent();
    CHECK(s.size() == 1, "sends_plan_within_poll: %d messages after one poll, not 1", (int)s.size());
    if (s.size() == 1)
    {
        CHECK((int)s[0].size() == kPlanLen, "sends_plan_within_poll: %d bytes, not %d", (int)s[0].size(), kPlanLen);
        CHECK(isPlan(s[0], planOf(3)), "sends_plan_within_poll: not the Plan pushed");
        CHECK(s[0].front() == 0xF0 && s[0].back() == 0xF7, "sends_plan_within_poll: not one whole SysEx");
    }
    CHECK(locked([] { return fake.sentPort.size() == 1 && fake.sentPort[0] == 0; }), "sends_plan_within_poll: not from the out port");
    CHECK(locked([] { return fake.name; }) == "Third Planet", "sends_plan_within_poll: the client's name");
    CHECK(locked([] { return fake.nonblocking; }), "sends_plan_within_poll: the handle may block the worker");
    r.clock.ms(100);
    CHECK(sent().size() == 1, "sends_plan_within_poll: the same Plan sent again within 100 ms");
}

void coalesces_to_20_per_s()
{
    Rig r;
    r.start();
    for (int i = 0; i < 100; ++i)   // one Plan every 10 ms (5 polls), over 1 s
    {
        r.plan(planOf(i + 1));
        r.clock.step(5);
    }
    const auto s = sent();
    const int n = countType(s, kPlan);
    CHECK(n <= 20, "coalesces_to_20_per_s: %d PLANs in 1 s", n);
    CHECK(n >= 15, "coalesces_to_20_per_s: only %d PLANs in 1 s", n);
    CHECK((int)s.size() == n, "coalesces_to_20_per_s: other messages");
    r.clock.ms(60);
    const auto s2 = sent();
    CHECK(!s2.empty() && isPlan(s2.back(), planOf(100)), "coalesces_to_20_per_s: the last Plan pushed was not the last sent");
    CHECK(s2.size() <= s.size() + 1, "coalesces_to_20_per_s: %d more PLANs for one pending", (int)(s2.size() - s.size()));
    for (size_t i = 1; i < s2.size(); ++i)
        CHECK(s2[i][6] == ((s2[i - 1][6] + 1) & 0x7F), "coalesces_to_20_per_s: seq %d after %d", s2[i][6], s2[i - 1][6]);
}

void heartbeat_every_2s()
{
    Rig r;
    r.start();
    r.plan(planOf(7));
    r.clock.step();
    CHECK(sent().size() == 1, "heartbeat_every_2s: the Plan was not sent");
    r.clock.ms(10000);
    const auto s = sent();
    CHECK(s.size() == 6, "heartbeat_every_2s: %d PLANs in 10 s with nothing pushed, not 5", (int)s.size() - 1);
    for (const Bytes& m : s)
        CHECK(isPlan(m, planOf(7)), "heartbeat_every_2s: a heartbeat is not the last state");
}

void no_heartbeat_before_a_plan()
{
    Rig r;
    r.start();
    r.clock.ms(5000);
    CHECK(sent().empty(), "no_heartbeat_before_a_plan: %d messages with no Plan ever pushed", (int)sent().size());
}

void events_in_order()
{
    Rig r;
    r.start();
    for (int i = 0; i < 5; ++i)
        r.event(100 + i);
    r.clock.step();
    const auto s = sent();
    CHECK(s.size() == 5, "events_in_order: %d messages, not 5", (int)s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        uint8_t buf[kMaxMsg];
        const LinkEvent e{0, 3, -1, 0, 0, (int16_t)(100 + i)};
        CHECK((int)s[i].size() == kEventLen && typeOf(s[i]) == kEvent, "events_in_order: message %d is not an EVENT", (int)i);
        CHECK(encodeEvent(e, s[i][6], buf) == kEventLen && std::memcmp(buf, s[i].data(), kEventLen) == 0,
              "events_in_order: message %d is not event %d", (int)i, (int)i);
        if (i) CHECK(s[i][6] == ((s[i - 1][6] + 1) & 0x7F), "events_in_order: seq %d after %d", s[i][6], s[i - 1][6]);
    }
    // an Event and the Plan that shows it: the Event first, both in the same poll
    r.event(200);
    r.plan(planOf(9));
    r.clock.ms(60);
    const auto s2 = sent();
    CHECK(s2.size() == 7 && typeOf(s2[5]) == kEvent && isPlan(s2[6], planOf(9)), "events_in_order: the Plan did not follow its Event");
}

void bye_on_stop()
{
    Rig r;
    r.start();
    r.plan(planOf(1));
    r.clock.step();
    r.stop();
    const auto s = sent();
    CHECK(s.size() >= 2 && typeOf(s.back()) == kBye && (int)s.back().size() == kByeLen, "bye_on_stop: the last message is not a BYE");
    CHECK(countType(s, kBye) == 1, "bye_on_stop: %d BYEs", countType(s, kBye));
    CHECK(locked([] { return fake.opens == 1 && fake.closes == 1; }), "bye_on_stop: not closed");
    CHECK(locked([] { return fake.sendsWhileClosed; }) == 0, "bye_on_stop: a send after the close");
}

void port_caps()
{
    Rig r;
    r.start();
    const auto ports = locked([] { return fake.ports; });
    CHECK(ports.size() == 2, "port_caps: %d ports, not 2", (int)ports.size());
    if (ports.size() == 2)
    {
        CHECK(ports[0].name == "Harmony Link", "port_caps: the out port is \"%s\"", ports[0].name.c_str());
        CHECK(ports[0].caps == 1u, "port_caps: the out port's caps are %#x, not READ only", ports[0].caps);
        CHECK(!(ports[0].caps & (1u << 5)), "port_caps: the out port can be subscribed to (echo)");
        CHECK(ports[1].caps == ((1u << 1) | (1u << 6)), "port_caps: the in port's caps are %#x", ports[1].caps);
    }
    const auto to = locked([] { return fake.to; });
    const auto from = locked([] { return fake.from; });
    CHECK(to.size() == 1 && to[0].port == 0 && to[0].client == 24 && to[0].cport == 0, "port_caps: out is not connected to 24:0");
    CHECK(from.size() == 1 && from[0].port == 1 && from[0].client == 24 && from[0].cport == 0, "port_caps: in is not connected from 24:0");
}

void worker_reconnects_after_target_loss()
{
    Rig r;
    locked([] { fake.client = -1; return 0; });
    r.start();
    r.plan(planOf(1));
    for (int i = 0; i < 5; ++i) r.event(i);
    r.clock.ms(3000);
    CHECK(sent().empty(), "reconnect: %d messages with no f_midi", (int)sent().size());
    CHECK(locked([] { return fake.to.empty(); }), "reconnect: connected to nothing");
    const int finds = locked([] { return fake.finds; });
    CHECK(finds >= 2 && finds <= 3, "reconnect: looked for f_midi %d times in 3 s, not every 2 s", finds);
    r.plan(planOf(2));
    locked([] { fake.client = 24; return 0; });
    r.clock.ms(2000);
    auto to = locked([] { return fake.to; });
    CHECK(to.size() == 1 && to[0].client == 24 && to[0].cport == 0, "reconnect: no connectTo(24, 0) within 2 s");
    auto s = sent();
    CHECK(s.size() >= 1 && isPlan(s[0], planOf(2)), "reconnect: the newest Plan was not sent on connecting");
    CHECK(countType(s, kEvent) == 0, "reconnect: Events from before the connection were sent");
    r.plan(planOf(3));
    r.clock.ms(60);
    s = sent();
    CHECK(isPlan(s.back(), planOf(3)), "reconnect: sends did not resume");

    // a send error: a new client, a new connection, and the Plan that failed goes out
    const size_t before = s.size();
    locked([] { fake.sendErr = -19; return 0; });
    r.plan(planOf(4));
    r.clock.ms(60);
    CHECK(sent().size() == before, "reconnect: sent although the send failed");
    r.clock.ms(2000);
    to = locked([] { return fake.to; });
    CHECK(to.size() == 2 && to[1].client == 24, "reconnect: no reconnect after a send error (%d connects)", (int)to.size());
    CHECK(locked([] { return fake.opens == 2 && fake.closes == 1; }), "reconnect: not a new client after a send error");
    s = sent();
    CHECK(s.size() > before && isPlan(s.back(), planOf(4)), "reconnect: the Plan that failed was not sent after reconnecting");

    // every send failing: no faster than every 2 s
    const int opens = locked([] { fake.sendErrAlways = -19; return fake.opens; });
    r.clock.ms(10000);
    const int more = locked([] { return fake.opens; }) - opens;
    CHECK(more >= 3 && more <= 6, "reconnect: %d new clients in 10 s of failing sends", more);

    // -EAGAIN is not an error: no reconnect, the Plan goes out on the next poll
    locked([] { fake.sendErrAlways = 0; return 0; });
    r.clock.ms(2100);
    const int opens2 = locked([] { return fake.opens; });
    const size_t n2 = sent().size();
    locked([] { fake.sendErr = -11; return 0; });
    r.plan(planOf(5));
    r.clock.ms(60);
    s = sent();
    CHECK(s.size() == n2 + 1 && isPlan(s.back(), planOf(5)), "reconnect: a busy send was not tried again");
    CHECK(locked([] { return fake.opens; }) == opens2, "reconnect: a busy send made a new client");
    r.stop();
    CHECK(locked([] { return fake.opens == fake.closes; }), "reconnect: opens and closes differ");
}

// f_midi goes (the subscription with it) and sends to subscribers still succeed: found by looking every 2 s
void finds_silent_loss()
{
    Rig r;
    r.start();
    r.plan(planOf(1));
    r.clock.step();
    CHECK(sent().size() == 1, "finds_silent_loss: the Plan was not sent");
    locked([] { fake.client = -1; return 0; });
    r.clock.ms(2000);
    const size_t n = sent().size();
    r.clock.ms(6000);
    CHECK(sent().size() == n, "finds_silent_loss: still sending %d s after f_midi went", 6);
    locked([] { fake.client = 25; return 0; });   // (back, as another client)
    r.clock.ms(2000);
    const auto to = locked([] { return fake.to; });
    CHECK(to.size() == 2 && to[1].client == 25, "finds_silent_loss: not connected to the new f_midi");
    CHECK(isPlan(sent().back(), planOf(1)), "finds_silent_loss: no Plan on connecting again");
}

void reads_fade_command()
{
    Rig r;
    r.start();
    const uint32_t seq0 = Worker::fadeCount(r.w->fadeCmd.load());
    CHECK(Worker::fadeLevel(r.w->fadeCmd.load()) == 127 && Worker::fadeBars(r.w->fadeCmd.load()) == 0, "reads_fade_command: the level is not full at first");
    queueInput({0xF0, 0x7D, 0x54, 0x50, 0x01, 0x10});
    queueInput({0x00, 0x00, 0x10, 0x00, 0xF7});
    r.clock.step();
    CHECK(Worker::fadeLevel(r.w->fadeCmd.load()) == 0, "reads_fade_command: level %u, not 0", Worker::fadeLevel(r.w->fadeCmd.load()));
    CHECK(Worker::fadeBars(r.w->fadeCmd.load()) == 16, "reads_fade_command: bars %u, not 16", Worker::fadeBars(r.w->fadeCmd.load()));
    CHECK(Worker::fadeCount(r.w->fadeCmd.load()) == seq0 + 1, "reads_fade_command: fadeSeq moved by %u", Worker::fadeCount(r.w->fadeCmd.load()) - seq0);
    CHECK(r.w->holdCmd.load() == -1, "reads_fade_command: a Hold out of a FADE");

    // one byte an event, the Mac's clock between the pieces
    const Bytes fade = {0xF0, 0x7D, 0x54, 0x50, 0x01, 0x10, 0x05, 0x40, 0x04, 0x00, 0xF7};
    for (uint8_t b : fade)
    {
        queueInput({b});
        queueInput({}, 36);   // (SND_SEQ_EVENT_CLOCK)
    }
    r.clock.step();
    CHECK(Worker::fadeLevel(r.w->fadeCmd.load()) == 0x40 && Worker::fadeBars(r.w->fadeCmd.load()) == 4 && Worker::fadeCount(r.w->fadeCmd.load()) == seq0 + 2,
          "reads_fade_command: a FADE one byte at a time: level %u bars %u", Worker::fadeLevel(r.w->fadeCmd.load()), Worker::fadeBars(r.w->fadeCmd.load()));

    // not ours, malformed, cut off, too long: nothing moves
    queueInput({0xF0, 0x00, 0x20, 0x3C, 0x01, 0x10, 0x00, 0x00, 0x10, 0x00, 0xF7});   // (another maker's)
    queueInput({0xF0, 0x7D, 0x54, 0x50, 0x02, 0x10, 0x00, 0x00, 0x10, 0x00, 0xF7});   // (another version)
    queueInput({0xF0, 0x7D, 0x54, 0x50, 0x01, 0x10, 0x00, 0x00});                     // (cut off ...
    queueInput({0xF0, 0x7D, 0x54, 0x50, 0x01, 0x10, 0x00, 0x00, 0x10});               //  ... by another start, cut off too)
    Bytes big = {0xF0, 0x7D, 0x54, 0x50, 0x01, 0x10};
    big.resize(400, 0x00);
    big.push_back(0xF7);
    queueInput(big);                                                                   // (over kMaxMsg)
    queueInput({0x00, 0x00, 0x10, 0x00, 0xF7});                                        // (a tail with no start)
    r.clock.step();
    CHECK(Worker::fadeCount(r.w->fadeCmd.load()) == seq0 + 2, "reads_fade_command: a malformed message moved fadeSeq");
    CHECK(Worker::fadeLevel(r.w->fadeCmd.load()) == 0x40 && Worker::fadeBars(r.w->fadeCmd.load()) == 4, "reads_fade_command: a malformed message moved the level");
    // ... and the reader is whole again after them
    queueInput({0xF0, 0x7D, 0x54, 0x50, 0x01, 0x10, 0x00, 0x7F, 0x00, 0x00, 0xF7});
    r.clock.step();
    CHECK(Worker::fadeLevel(r.w->fadeCmd.load()) == 127 && Worker::fadeBars(r.w->fadeCmd.load()) == 0 && Worker::fadeCount(r.w->fadeCmd.load()) == seq0 + 3,
          "reads_fade_command: no FADE read after the malformed ones");
    CHECK(sent().empty(), "reads_fade_command: a command made the worker send");
    // two FADEs in one pass: the reader's one word is the second, whole (never one's level with the other's bars),
    // and the same FADE again is a new word
    const uint32_t w0 = r.w->fadeCmd.load();
    queueInput({0xF0, 0x7D, 0x54, 0x50, 0x01, 0x10, 0x00, 0x00, 0x7F, 0x7F, 0xF7});   // (level 0, bars 16383)
    queueInput({0xF0, 0x7D, 0x54, 0x50, 0x01, 0x10, 0x00, 0x7F, 0x02, 0x00, 0xF7});   // (level 127, bars 2)
    r.clock.step();
    const uint32_t w1 = r.w->fadeCmd.load();
    CHECK(Worker::fadeLevel(w1) == 127 && Worker::fadeBars(w1) == 2 && Worker::fadeCount(w1) == Worker::fadeCount(w0) + 2,
          "reads_fade_command: two FADEs in a pass: level %u bars %u", Worker::fadeLevel(w1), Worker::fadeBars(w1));
    queueInput({0xF0, 0x7D, 0x54, 0x50, 0x01, 0x10, 0x00, 0x7F, 0x02, 0x00, 0xF7});
    r.clock.step();
    const uint32_t w2 = r.w->fadeCmd.load();
    CHECK(w2 != w1 && Worker::fadeLevel(w2) == 127 && Worker::fadeBars(w2) == 2, "reads_fade_command: the same FADE again is not a new word");
    // the count goes round in its 11 bits; the level and the bars stay whole
    for (int i = 0; i < 2100; ++i)
        r.w->postFade(5, 16383);
    const uint32_t w3 = r.w->fadeCmd.load();
    CHECK(Worker::fadeLevel(w3) == 5 && Worker::fadeBars(w3) == 16383 && Worker::fadeCount(w3) < 2048,
          "reads_fade_command: after 2100 FADEs: level %u bars %u count %u", Worker::fadeLevel(w3), Worker::fadeBars(w3), Worker::fadeCount(w3));
}

void reads_hold_command()
{
    Rig r;
    r.start();
    CHECK(r.w->holdCmd.load() == -1, "reads_hold_command: holdCmd is not -1 at first");
    queueInput({0xF0, 0x7D, 0x54, 0x50, 0x01, 0x11, 0x00, 0x01, 0xF7});
    r.clock.step();
    CHECK(r.w->holdCmd.load() == 1, "reads_hold_command: holdCmd %d, not 1", r.w->holdCmd.load());
    CHECK(r.w->holdCmd.exchange(-1) == 1, "reads_hold_command: the exchange");
    r.clock.step(10);
    CHECK(r.w->holdCmd.load() == -1, "reads_hold_command: read twice");
    queueInput({0xF0, 0x7D, 0x54, 0x50, 0x01, 0x11, 0x01});
    queueInput({0x00, 0xF7});
    r.clock.step();
    CHECK(r.w->holdCmd.load() == 0, "reads_hold_command: holdCmd %d, not 0 (off)", r.w->holdCmd.load());
    CHECK(Worker::fadeCount(r.w->fadeCmd.load()) == 0, "reads_hold_command: a FADE out of a HOLD");
}

// What was left in the ring before the link opened is not sent; a fresh Plan is asked of the audio thread
void drops_stale_items_on_open()
{
    Rig r;
    r.plan(planOf(1));
    for (int i = 0; i < 5; ++i) r.event(i);
    CHECK(!r.w->takeWantPlan(), "stale: a Plan wanted before the link opened");
    r.start();
    r.clock.ms(100);
    CHECK(sent().empty(), "stale: %d stale messages sent", (int)sent().size());
    Item it{};
    CHECK(!r.ring.pop(it), "stale: the ring was not drained");
    CHECK(r.w->takeWantPlan(), "stale: no fresh Plan asked for");
    CHECK(!r.w->takeWantPlan(), "stale: asked twice");
    r.plan(planOf(2));
    r.clock.step();
    CHECK(sent().size() == 1 && isPlan(sent()[0], planOf(2)), "stale: the fresh Plan was not sent");
}

// The plugin's use: the thread is made once and idles; the audio thread only sets a flag
void enable_flag()
{
    Rig r;
    r.w->setEnabled(false);
    r.start();
    r.clock.step(200);
    CHECK(locked([] { return fake.opens; }) == 0, "enable_flag: opened while off");
    CHECK(!r.w->takeWantPlan(), "enable_flag: a Plan wanted while off");
    r.w->setEnabled(true);
    r.clock.step(2);
    CHECK(locked([] { return fake.opens; }) == 1, "enable_flag: not opened when on");
    CHECK(r.w->takeWantPlan(), "enable_flag: no Plan asked for when on");
    r.plan(planOf(1));
    r.clock.step();
    CHECK(sent().size() == 1 && isPlan(sent()[0], planOf(1)), "enable_flag: the Plan was not sent");
    r.w->setEnabled(false);
    r.clock.step(2);
    auto s = sent();
    CHECK(s.size() == 2 && typeOf(s.back()) == kBye, "enable_flag: no BYE when turned off");
    CHECK(locked([] { return fake.closes; }) == 1, "enable_flag: not closed when turned off");
    r.clock.step(500);
    CHECK(sent().size() == 2, "enable_flag: sent while off");
    r.plan(planOf(2));   // (left from before: the audio thread pushes nothing while off)
    r.w->setEnabled(true);
    r.clock.step(3);
    CHECK(sent().size() == 2, "enable_flag: a stale Plan sent when on again");
    CHECK(r.w->takeWantPlan(), "enable_flag: no Plan asked for when on again");
    r.plan(planOf(3));
    r.clock.step();
    s = sent();
    CHECK(s.size() == 3 && isPlan(s.back(), planOf(3)), "enable_flag: no Plan when on again");
    r.stop();
    s = sent();
    CHECK(typeOf(s.back()) == kBye && locked([] { return fake.opens == 2 && fake.closes == 2; }), "enable_flag: stop did not close");
}

void worker_toggle_stress()
{
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    fake.reset();
    LinkRing ring;
    FreeClock clock;
    Worker w(ring, fakeApi(), clock);
    std::atomic<bool> quit{false};
    std::thread audio([&] {   // (the audio thread goes on pushing whatever the worker does)
        int n = 0;
        while (!quit.load())
        {
            Item it{};
            it.kind = (n & 3) ? Item::Event : Item::Plan;
            it.plan = planOf(n);
            it.ev = LinkEvent{0, 3, -1, 0, 0, (int16_t)(n & 0x3FF)};
            ring.push(it);
            if (w.takeWantPlan()) { it.kind = Item::Plan; ring.push(it); }
            if ((++n & 63) == 0) std::this_thread::yield();
        }
    });
    for (int i = 0; i < 200; ++i)
    {
        w.start();
        if (i % 3 == 1) std::this_thread::yield();
        if (i % 3 == 2) std::this_thread::sleep_for(std::chrono::microseconds(200));
        if (i % 5 == 0) w.setEnabled(i % 10 == 0);
        w.stop();
    }
    w.stop();   // (twice: nothing)
    quit.store(true);
    audio.join();
    const double secs = std::chrono::duration<double>(clk::now() - t0).count();
    CHECK(secs < 5.0, "worker_toggle_stress: %.1f s", secs);
    const int opens = locked([] { return fake.opens; }), closes = locked([] { return fake.closes; });
    CHECK(opens == closes, "worker_toggle_stress: %d opens, %d closes", opens, closes);
    CHECK(opens > 0, "worker_toggle_stress: never opened");
    CHECK(locked([] { return fake.sendsWhileClosed; }) == 0, "worker_toggle_stress: sends after a close");
    const auto s = sent();
    for (const Bytes& m : s)
        if (typeOf(m) < 0 || m.back() != 0xF7) { CHECK(false, "worker_toggle_stress: a message is not Harmony Link's"); break; }
    std::printf("worker_toggle_stress: %d opens, %d messages, %.2f s\n", opens, (int)s.size(), secs);
}

// Link On and Off as the plugin does it: the flag flipped on a running worker, the audio thread pushing all the while
void enable_toggle_stress()
{
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    fake.reset();
    LinkRing ring;
    FreeClock clock;
    Worker w(ring, fakeApi(), clock);
    std::atomic<bool> quit{false};
    std::thread audio([&] {
        int n = 0;
        while (!quit.load())
        {
            Item it{};
            it.kind = (n & 3) ? Item::Event : Item::Plan;
            it.plan = planOf(n);
            it.ev = LinkEvent{0, 3, -1, 0, 0, (int16_t)(n & 0x3FF)};
            ring.push(it);
            if (w.takeWantPlan()) { it.kind = Item::Plan; ring.push(it); }
            if ((++n & 31) == 0) std::this_thread::sleep_for(std::chrono::microseconds(50));   // (a block's worth, then the next)
        }
    });
    w.start();
    std::thread hand([&] {
        for (int i = 0; i < 400; ++i)
        {
            w.setEnabled(i & 1);
            if (i % 3 == 0) std::this_thread::yield();
            if (i % 3 == 1) std::this_thread::sleep_for(std::chrono::microseconds(100 + 37 * (i % 7)));
        }
        w.setEnabled(true);
    });
    hand.join();
    // on at the end: the link opens and a PLAN goes out
    const int opens0 = locked([] { return fake.opens; });
    for (int i = 0; i < 4000 && w.state.load() != Worker::Connected; ++i)
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    size_t seen = locked([] { return fake.sent.size(); });
    bool plan = false;
    for (int i = 0; i < 4000 && !plan; ++i)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        locked([&] {
            for (; seen < fake.sent.size() && !plan; ++seen)
                plan = typeOf(fake.sent[seen]) == kPlan;
            return 0;
        });
    }
    CHECK(w.state.load() == Worker::Connected, "enable_toggle_stress: not connected after the last On");
    CHECK(plan, "enable_toggle_stress: no PLAN after the last On");
    quit.store(true);
    audio.join();
    w.stop();
    const double secs = std::chrono::duration<double>(clk::now() - t0).count();
    CHECK(secs < 5.0, "enable_toggle_stress: %.1f s", secs);
    const int opens = locked([] { return fake.opens; }), closes = locked([] { return fake.closes; });
    CHECK(opens == closes, "enable_toggle_stress: %d opens, %d closes", opens, closes);
    CHECK(opens >= 2 && opens >= opens0, "enable_toggle_stress: opened %d times in 400 flips", opens);
    CHECK(locked([] { return fake.sendsWhileClosed; }) == 0, "enable_toggle_stress: sends after a close");
    const auto s = sent();
    CHECK(!s.empty() && typeOf(s.back()) == kBye, "enable_toggle_stress: the last message is not a BYE");
    for (const Bytes& m : s)
        if (typeOf(m) < 0 || m.back() != 0xF7) { CHECK(false, "enable_toggle_stress: a message is not Harmony Link's"); break; }
    std::printf("enable_toggle_stress: %d opens, %d messages, %.2f s\n", opens, (int)s.size(), secs);
}

void no_alsa_degrades()
{
    Rig r(false);
    r.start();   // (returns)
    int dropped = 0;
    for (int i = 0; i < 2000; ++i)   // far more than the ring holds
    {
        Item it{};
        it.kind = Item::Event;
        it.ev = LinkEvent{0, 3, -1, 0, 0, (int16_t)(i & 0xFF)};
        dropped += !r.ring.push(it);
        if (i % 50 == 0) { r.plan(planOf(i)); r.clock.step(); }
    }
    r.clock.step(2);
    Item it{};
    CHECK(!r.ring.pop(it), "no_alsa_degrades: the ring is not drained");
    CHECK(dropped == 0 && r.ring.dropped() == 0, "no_alsa_degrades: %d Events found the ring full", dropped);
    CHECK(sent().empty() && locked([] { return fake.opens == 0 && fake.finds == 0; }), "no_alsa_degrades: the sequencer was used");
    r.stop();
    CHECK(locked([] { return fake.closes; }) == 0, "no_alsa_degrades: closed what was never open");
}

// The real loader: here (no libasound on the Mac) it must say so and leave nothing to call by mistake; with one
// (Linux) it must have every function.
void load_alsa()
{
    const SeqApi a = loadAlsa();
    if (!a.ok) { std::printf("load_alsa: no libasound here (ok = false)\n"); return; }
    CHECK(a.open && a.close && a.setName && a.createPort && a.outputDirect && a.connectTo && a.connectFrom &&
          a.nonblock && a.input && a.findClient, "load_alsa: ok with a function missing");
}


// The audio thread stopped (no block for kStaleMs): no heartbeat, so the computer sees the plan go stale; it comes back
// with the blocks.
void heartbeat_stops_without_blocks()
{
    Rig r;
    r.start();
    r.plan(planOf(4));
    r.clock.step();
    CHECK(sent().size() == 1, "stale_heartbeat: the Plan was not sent");
    r.clock.ticking = false;
    r.clock.ms(10000);
    CHECK(sent().size() == 1, "stale_heartbeat: %d heartbeats in 10 s with no block", (int)sent().size() - 1);
    r.clock.ticking = true;   // (the next heartbeat at once: the last was long ago)
    r.clock.ms(1900);
    const auto s = sent();
    CHECK(s.size() == 2 && isPlan(s.back(), planOf(4)), "stale_heartbeat: %d heartbeats once blocks came back, not 1",
          (int)s.size() - 1);
    r.clock.ticking = false;   // (blocks stop again: the heartbeat 0.1 s later still goes, as the blocks were fresh)
    r.clock.ms(1700);
    CHECK(sent().size() == 3, "stale_heartbeat: %d messages, not a heartbeat 2 s after the last with blocks 0.1 s ago",
          (int)sent().size());
    r.clock.ms(6000);
    CHECK(sent().size() == 3, "stale_heartbeat: heartbeats after the blocks stopped again");
}

// Link Off: the thread sleeps kIdleMs (100 ms) a pass, and stop() still joins it about as fast. With no libasound the
// ring is drained every kNoAlsaMs.
void idle_sleeps_100ms()
{
    {
        Rig r;
        r.w->setEnabled(false);
        r.start();
        const long long t0 = r.clock.t;
        r.clock.step(10);
        CHECK(r.clock.t - t0 == 10 * Worker::kIdleMs && Worker::kIdleMs == 100, "idle: %lld ms in 10 idle passes",
              r.clock.t - t0);
    }
    {
        Rig r(false);
        r.start();
        const long long t0 = r.clock.t;
        r.clock.step(10);
        CHECK(r.clock.t - t0 == 10 * Worker::kNoAlsaMs && Worker::kNoAlsaMs <= 20, "idle: %lld ms in 10 drains with no ALSA",
              r.clock.t - t0);
    }
    using clk = std::chrono::steady_clock;
    LinkRing ring;
    Worker w(ring, fakeApi(), systemClock());
    w.setEnabled(false);
    w.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto t0 = clk::now();
    w.stop();
    const double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    CHECK(ms < 150.0, "idle: stop() took %.0f ms with Link Off", ms);
}

// The system has no thread to give (std::thread throws; every instance makes one, host scans included): start()
// returns, the link does nothing, stop() and the destructor are fine, and a later start() may still succeed.
void start_survives_no_thread()
{
    fake.reset();
    LinkRing ring;
    StepClock clock;
    clock.release();
    {
        Worker w(ring, fakeApi(), clock);
        w.failStart = true;
        bool threw = false;
        try { w.start(); } catch (...) { threw = true; }
        CHECK(!threw, "no_thread: start() threw");
        CHECK(!w.running(), "no_thread: a thread anyway");
        Item it{};
        it.kind = Item::Plan;
        ring.push(it);
        CHECK(!w.takeWantPlan() && w.state.load() == Worker::Off, "no_thread: the link opened");
        w.stop();   // (nothing to join)
        w.failStart = false;
        w.start();
        CHECK(w.running(), "no_thread: no thread when the system has one again");
        for (int i = 0; i < 2000 && w.state.load() != Worker::Connected; ++i)
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        CHECK(w.state.load() == Worker::Connected, "no_thread: the link did not open on the second start");
    }   // (the destructor joins)
    CHECK(locked([] { return fake.opens == fake.closes; }), "no_thread: the client left open");
}

} // namespace

int main()
{
    sends_plan_within_poll();
    coalesces_to_20_per_s();
    heartbeat_every_2s();
    no_heartbeat_before_a_plan();
    events_in_order();
    bye_on_stop();
    port_caps();
    worker_reconnects_after_target_loss();
    finds_silent_loss();
    reads_fade_command();
    reads_hold_command();
    drops_stale_items_on_open();
    enable_flag();
    worker_toggle_stress();
    enable_toggle_stress();
    no_alsa_degrades();
    load_alsa();
    heartbeat_stops_without_blocks();
    idle_sleeps_100ms();
    start_survives_no_thread();
    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("test_link_worker: all passed\n");
    return 0;
}
