// SPDX-License-Identifier: GPL-3.0-only
// Harmony Link's capture on the audio thread (docs/harmony-link.md),
// through the plugin class with a fake host that answers audioMasterGetTime, draining linkRing_ directly:
//   - Link Off pushes nothing; Link On pushes a Plan when Link turns on and then only on a change;
//   - a hand's Next chord comes out as an Event, then a Plan with the new chord; Evolve's values are not forwarded;
//   - the Plan carries the readouts (app_api.h) and the host's position in 16ths; chordMask() is the engine's chord;
//   - the ring keeps Events in push order (dropping the newest when full) and never loses the latest Plan;
//   - with Link On the SKY log and the audio are what they are with Link Off.
//   - the plugin's own link thread takes the ring with Link On and idles with Link Off (every other test keeps it
//     idle, linkSend_ = false, and pops the ring itself);
//   - the computer's commands (5.6), written into the worker's atomics here as the worker would: FADE ramps the output
//     in host bars, HOLD holds, a transport START resets both (a hold set by hand stays), Link Off ignores them;
//   - Harmony Link's own SysEx (and any other) delivered to the plugin changes nothing.
// TP_LINK=1 only.
//   c++ -std=c++17 -O2 -DTP_LINK=1 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework \
//       plugins/ThirdPlanet/tests/test_link.cpp plugins/ThirdPlanet/src/app_unit.cpp \
//       plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp plugins/ThirdPlanet/src/sky.cpp \
//       plugins/ThirdPlanet/src/link_codec.cpp plugins/ThirdPlanet/src/link_capture.cpp \
//       plugins/ThirdPlanet/src/link_alsa.cpp plugins/ThirdPlanet/src/link_worker.cpp \
//       -o plugins/ThirdPlanet/build/tests/test_link
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ThirdPlanet.cpp"

#if !TP_LINK
#error "test_link needs TP_LINK=1"
#endif

#include "link_capture.h"
#include "link_ring.h"

namespace {

int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

using tp::link::Item;
using tp::link::PlanState;

VstTimeInfo ti;
VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterGetTime) return (VstIntPtr)&ti;
    return 0;
}
AEffect fakeFx;

int idx(const char* id)
{
    for (int i = 0; i < tp::kNumControls; ++i)
        if (!std::strcmp(tp::kParams[i].id, id)) return i;
    std::printf("FAIL: no parameter %s\n", id);
    std::exit(1);
}

std::unique_ptr<ThirdPlanet> make(bool worker = false)
{
    std::memset(&ti, 0, sizeof ti);
    ti.sampleRate = 44100, ti.tempo = 120, ti.flags = kVstTempoValid | kVstPpqPosValid;   // (the MPC stopped)
    auto p = std::make_unique<ThirdPlanet>();
    p->linkSend_ = worker;   // (false: the link's thread stays idle, and the test is the ring's one consumer)
    p->effect_ = &fakeFx;
    p->host_ = hostCb;
    p->internalSetSampleRate(44100.0);
    p->internalSetBlockSize(128);
    p->internalResume();
    return p;
}

void setLink(ThirdPlanet& p, bool on) { p.setNormalised(idx("link"), on ? 1.0f : 0.0f); }   // (as the host's setParameter)

float L[128], R[128];
uint64_t audioHash = 1469598103934665603ull;
void block(ThirdPlanet& p)
{
    float* outs[2] = {L, R};
    p.internalProcess(nullptr, outs, 128);
    for (int i = 0; i < 128; ++i)
    {
        uint32_t a, b;
        std::memcpy(&a, &L[i], 4), std::memcpy(&b, &R[i], 4);
        audioHash = (audioHash ^ a) * 1099511628211ull, audioHash = (audioHash ^ b) * 1099511628211ull;
    }
    if (ti.flags & kVstTransportPlaying) ti.ppqPos += 128 * ti.tempo / 60.0 / 44100.0;
}
void run(ThirdPlanet& p, double secs)
{
    for (long done = 0; done < (long)(secs * 44100); done += 128)
        block(p);
}

std::vector<Item> drain(ThirdPlanet& p)
{
    std::vector<Item> v;
    Item it{};
    while (p.linkRing_.pop(it))
        v.push_back(it);
    return v;
}
int count(const std::vector<Item>& v, Item::Kind k)
{
    int n = 0;
    for (const Item& it : v)
        n += it.kind == k;
    return n;
}
bool sameChord(const tp::link::Chord& a, const tp::link::Chord& b)
{
    return a.root == b.root && a.mask == b.mask && std::memcmp(a.name, b.name, 8) == 0;
}

void no_items_when_link_off()
{
    auto p = make();
    run(*p, 5.0);
    tp::app::nextChord();   // (a hand action too)
    run(*p, 0.5);
    CHECK(drain(*p).empty(), "no_items_when_link_off: the ring has items with Link Off");
}

void plan_on_change_only()
{
    auto p = make();
    p->setNormalised(idx("sync"), 1.0f);   // Sync on with the MPC stopped: the piece rests
    run(*p, 8.0);                          // (its tails die away)
    CHECK(!tp::app::playing(), "plan_on_change_only: the piece did not rest");
    CHECK(drain(*p).empty(), "plan_on_change_only: items before Link On");
    setLink(*p, true);
    block(*p);
    const std::vector<Item> first = drain(*p);
    CHECK(first.size() == 1 && count(first, Item::Plan) == 1, "plan_on_change_only: %d items (%d Plans) after the first block, not one Plan",
          (int)first.size(), count(first, Item::Plan));
    int n = 0;
    for (long done = 0; done < 2 * 44100; done += 128)
    {
        block(*p);
        n += (int)drain(*p).size();
    }
    CHECK(n == 0, "plan_on_change_only: %d items in 2 s of unchanged state", n);
    // Link Off and On again: a Plan at once (the receiver may have missed everything meanwhile)
    setLink(*p, false);
    run(*p, 0.5);
    CHECK(drain(*p).empty(), "plan_on_change_only: items with Link Off again");
    setLink(*p, true);
    block(*p);
    CHECK(count(drain(*p), Item::Plan) == 1, "plan_on_change_only: no Plan when Link turned on again");
}

void next_chord_forwards_event_then_plan()
{
    auto p = make();
    setLink(*p, true);
    run(*p, 3.0);   // (playing on its own clock)
    std::vector<Item> before = drain(*p);
    CHECK(count(before, Item::Plan) > 0, "next_chord: no Plan while playing");
    PlanState was{};
    for (const Item& it : before)
        if (it.kind == Item::Plan) was = it.plan;
    tp::app::nextChord();   // a hand action, from the test (the audio thread's, between blocks)
    int evAt = -1, planAt = -1, k = 0, evs = 0;
    for (long done = 0; done < 6 * 44100 && planAt < 0; done += 128)   // (the chord changes on the next bar)
    {
        block(*p);
        for (const Item& it : drain(*p))
        {
            if (it.kind == Item::Event)
            {
                ++evs;
                if (it.ev.what == tp::app::kEvNextChord && it.ev.who == tp::app::kWhoYou && evAt < 0) evAt = k;
            }
            else if (planAt < 0 && !sameChord(it.plan.now, was.now))
                planAt = k;
            ++k;
        }
    }
    CHECK(evAt >= 0, "next_chord: no kEvNextChord Event from kWhoYou (%d Events)", evs);
    CHECK(planAt >= 0, "next_chord: no Plan with a new chord now");
    CHECK(evAt >= 0 && planAt > evAt, "next_chord: the Plan (%d) is not after the Event (%d)", planAt, evAt);
}

void evolve_params_not_forwarded()
{
    using namespace tp::app;
    // forwards(): every hand action, the piece; not Evolve's values
    for (int w = kEvParam; w <= kEvPiece; ++w)
    {
        CHECK(tp::link::forwards(Event{kWhoYou, (What)w, -1, -1, 0, 0}), "forwards: You's what %d not forwarded", w);
        CHECK(!tp::link::forwards(Event{kWhoEvolve, (What)w, 2, P_CUTOFF, 1, 2}) || w == kEvPiece,
              "forwards: Evolve's what %d forwarded", w);
    }
    CHECK(tp::link::forwards(Event{kWhoPiece, kEvPiece, -1, -1, 0, 3}), "forwards: the piece not forwarded");
    // through the plugin: Evolve at 100 nudges on phrases; none of it comes out as an Event
    auto p = make();
    run(*p, 1.0);   // (the plugin takes the app's values first)
    p->set(idx("evolve"), 100.0f);
    setLink(*p, true);
    run(*p, 0.1);
    CHECK(par(P_EVOLVE) == 100, "evolve: Evolve is %d, not 100", par(P_EVOLVE));
    bool evolveLogged = false;
    int evolveItems = 0, events = 0;
    for (long done = 0; done < 70L * 44100; done += 128)
    {
        block(*p);
        for (int l = 0; l < 3; ++l)
            evolveLogged |= std::strncmp(p->sky().logText(l), "Evolve", 6) == 0;
        for (const Item& it : drain(*p))
            if (it.kind == Item::Event)
            {
                ++events;
                evolveItems += it.ev.who == kWhoEvolve;
            }
    }
    CHECK(evolveLogged, "evolve: Evolve never nudged in 70 s (the test proves nothing)");
    CHECK(evolveItems == 0, "evolve: %d of Evolve's events forwarded (%d Events)", evolveItems, events);
}

void chord_mask_written_slot()
{
    using namespace tp::app;
    auto label = [](int root, int kind, int type) { return kChordLabelBase + (root * EN_NCQ + kind) * EN_NCX + type; };
    CHECK(tp::link::chordMask(label(0, EN_CQ_MAJOR, EN_CX_TRIAD)) == 0b000010010001, "C major triad: mask %03x",
          tp::link::chordMask(label(0, EN_CQ_MAJOR, EN_CX_TRIAD)));
    CHECK(tp::link::chordMask(label(7, EN_CQ_MAJOR, EN_CX_TRIAD)) == ((1 << 7) | (1 << 11) | (1 << 2)), "G major triad: mask %03x",
          tp::link::chordMask(label(7, EN_CQ_MAJOR, EN_CX_TRIAD)));
    CHECK(tp::link::chordMask(label(9, EN_CQ_MINOR, EN_CX_7)) == ((1 << 9) | (1 << 0) | (1 << 4) | (1 << 7)), "Am7: mask %03x",
          tp::link::chordMask(label(9, EN_CQ_MINOR, EN_CX_7)));
    // a root and its other pitch classes: D, F#, A
    CHECK(tp::link::chordMask(1 + 2 * 4096 + ((1 << 4) | (1 << 7))) == ((1 << 2) | (1 << 6) | (1 << 9)), "D F# A: mask %03x",
          tp::link::chordMask(1 + 2 * 4096 + ((1 << 4) | (1 << 7))));
    CHECK(tp::link::chordMask(0) == 0 && tp::link::chordMask(-5) == 0 && tp::link::chordMask(kChordCodes) == 0,
          "no chord: a mask");
    // every code: a mask within 12 bits with the root set
    for (int c = 1; c < kChordCodes; ++c)
    {
        const uint16_t m = tp::link::chordMask(c);
        const int root = c < kChordLabelBase ? (c - 1) / 4096 : (c - kChordLabelBase) / (EN_NCQ * EN_NCX);
        if (m >> 12 || !(m >> root & 1)) { CHECK(false, "code %d: mask %04x, root %d", c, m, root); break; }
    }
}

void capture_reads_readouts()
{
    using namespace tp::app;
    auto p = make();
    run(*p, 3.0);
    mpc::Transport t;
    t.valid = t.ppqValid = true;
    t.ppqPos = 10.5, t.barStartPpq = 8.0;   // 2.5 beats into the bar: 16th 10
    const PlanState s = tp::link::capture(t);
    CHECK(s.pos16 == 10, "pos16 %d, not 10", s.pos16);
    CHECK(s.playing == playing() && s.held == held() && s.locked == locked() && s.loopMode == loopMode() &&
          s.timed == external(), "capture: the flags");
    CHECK(s.piece == piece() && s.keyRoot == par(P_ROOT) % 12 && s.keyMode == par(P_MODE), "capture: piece or key");
    char name[kChordTextMax];
    chordText(chordCode(), name);
    CHECK(s.now.mask == tp::link::chordMask(chordCode()) && std::strncmp(s.now.name, name, 8) == 0 && s.now.root < 12,
          "capture: the chord now (%s vs %.8s)", name, s.now.name);
    chordText(nextChordCode(), name);
    CHECK(s.next.mask == tp::link::chordMask(nextChordCode()) && std::strncmp(s.next.name, name, 8) == 0, "capture: the next chord");
    CHECK(s.barsToNext == barsToNextChord() && s.barsToChange == barsToChange() && s.progPos == progPos() &&
          s.progCount == progCount() && s.walk == walkCharacter(), "capture: the progression");
    for (int i = 0; i < 8; ++i)
    {
        CHECK(s.slotState[i] == (uint8_t)slotState(i) && s.slot[i].mask == tp::link::chordMask(slotChordCode(i)),
              "capture: slot %d", i);
        CHECK(slotState(i) != kSlotGap || (s.slot[i].root == 127 && s.slot[i].name[0] == 0), "capture: gap %d", i);
    }
    CHECK(s.section == section() && s.phraseBar == phraseBar() && s.evolveChance == evolveChance() &&
          s.densityShift == densityShift(), "capture: the form");
    for (int l = 0; l < 5; ++l)
    {
        CHECK(((s.heardMask >> l) & 1) == (layerHeard(l) ? 1 : 0), "capture: heard %d", l);
        CHECK(s.activity[l] <= 127 && std::abs(s.activity[l] - layerActivity(l) * 127.0f) <= 0.5f, "capture: activity %d", l);
    }
    CHECK(s.heardMask >> 5 == 0, "capture: heardMask's high bits");
    // a slot written on the Chords page as C major, triad: its root, mask and name in the Plan
    knob(kKnobSlot, 3);
    setChordRoot(0);
    knob(kKnobChord, EN_CQ_MAJOR);
    knob(kKnobType, EN_CX_TRIAD);
    run(*p, 0.1);
    const PlanState w = tp::link::capture(t);
    const int code3 = slotChordCode(3);
    chordText(code3, name);
    CHECK(slotState(3) == kSlotWritten && w.slotState[3] == kSlotWritten, "capture: slot 3 not written (%d)", (int)slotState(3));
    CHECK(code3 == kChordLabelBase + (0 * EN_NCQ + EN_CQ_MAJOR) * EN_NCX + EN_CX_TRIAD, "capture: slot 3 is code %d, not C major", code3);
    CHECK(w.slot[3].root == 0 && w.slot[3].mask == 0b000010010001 && std::strncmp(w.slot[3].name, name, 8) == 0 && name[0] == 'C',
          "capture: written slot 3: root %d mask %03x name %.8s (%s)", w.slot[3].root, w.slot[3].mask, w.slot[3].name, name);
    CHECK(w == tp::link::capture(t), "capture twice: not equal");
    // pos16 at the edges, and with no position
    t.ppqPos = 11.99;
    CHECK(tp::link::capture(t).pos16 == 15, "pos16 at the bar's end");
    t.ppqPos = 8.0;
    CHECK(tp::link::capture(t).pos16 == 0, "pos16 on the bar line");
    t.ppqPos = 30.0;   // (a 22-beat bar: never past 15)
    CHECK(tp::link::capture(t).pos16 == 15, "pos16 past the bar");
    t.ppqPos = 7.0;
    CHECK(tp::link::capture(t).pos16 == 0, "pos16 before the bar line");
    t.ppqPos = NAN;
    CHECK(tp::link::capture(t).pos16 == 0, "pos16 not a number");
    CHECK(tp::link::capture(mpc::Transport{}).pos16 == 0, "pos16 with no transport");
}

void ring_order_and_capacity()
{
    static tp::link::Ring<8> r;
    Item it{};
    CHECK(!r.pop(it) && r.dropped() == 0, "ring: an item from an empty ring");
    for (int i = 0; i < 8; ++i)
    {
        Item e{};
        e.kind = Item::Event, e.ev.param = (int16_t)i;
        CHECK(r.push(e), "ring: push %d refused", i);
    }
    Item e{};
    e.kind = Item::Event, e.ev.param = 99;
    CHECK(!r.push(e) && r.dropped() == 1, "ring: the ninth Event into 8 kept");
    for (int i = 0; i < 8; ++i)
        CHECK(r.pop(it) && it.kind == Item::Event && it.ev.param == i, "ring: Event %d out of order", i);
    CHECK(!r.pop(it), "ring: not empty");
    // a Plan comes out after the Events queued with it
    Item pl{};
    pl.kind = Item::Plan, pl.plan.piece = 5;
    r.push(pl);
    r.push(e);
    CHECK(r.pop(it) && it.kind == Item::Event && r.pop(it) && it.kind == Item::Plan && it.plan.piece == 5 && !r.pop(it),
          "ring: not the Event then the Plan");
}

void ring_overflow_keeps_latest_plan()
{
    static tp::link::LinkRing r;
    for (int i = 0; i < 200; ++i)
    {
        Item e{};
        e.kind = Item::Event, e.ev.who = tp::app::kWhoYou, e.ev.what = tp::app::kEvNudge, e.ev.param = (int16_t)i;
        r.push(e);
        if (i == 10 || i == 100 || i == 190)
        {
            Item pl{};
            pl.kind = Item::Plan, pl.plan.piece = (uint8_t)(i == 10 ? 1 : i == 100 ? 2 : 3);
            r.push(pl);
        }
    }
    Item it{};
    int events = 0, plans = 0, last = -1;
    bool ordered = true;
    PlanState third{};
    third.piece = 3;
    while (r.pop(it))
    {
        if (it.kind == Item::Event)
        {
            ordered &= it.ev.param > last;
            last = it.ev.param, ++events;
        }
        else
        {
            ++plans;
            CHECK(it.plan == third, "overflow: the Plan out is piece %d, not the third", it.plan.piece);
        }
    }
    CHECK(events <= 128 && events > 0, "overflow: %d Events out", events);
    CHECK(ordered, "overflow: Events out of push order");
    CHECK(plans == 1, "overflow: %d Plans out, not one", plans);
    CHECK(r.dropped() > 0, "overflow: dropped() is 0");
}

// The producer and the consumer on two threads (run it once under -fsanitize=thread too): Events strictly in order,
// none lost but those counted in dropped(), every Plan whole (all its fields one counter), the last Plan the last
// pushed. Once with Events and Plans, once with Plans only (so the Events' indices cannot order the Plans).
void ring_two_threads(bool events)
{
    tp::link::Ring<16> r;   // (small: the queue fills and drops)
    constexpr int kPushes = 200000;
    std::atomic<bool> done{false};
    int popped = 0, plans = 0, torn = 0, disorder = 0, lastEv = -1, lastPlan = -1;
    std::thread consumer([&] {
        Item it{};
        for (;;)
        {
            const bool fin = done.load(std::memory_order_acquire);
            bool any = false;
            while (r.pop(it))
            {
                any = true;
                if (it.kind == Item::Event)
                {
                    const int v = (uint16_t)it.ev.param | (uint16_t)it.ev.from << 16;
                    disorder += v <= lastEv;
                    lastEv = v, ++popped;
                }
                else
                {
                    const int v = it.plan.section | it.plan.phraseBar << 8 | it.plan.piece << 16;
                    torn += it.plan.slot[7].mask != (uint16_t)(v & 0xFFFF) || it.plan.activity[4] != it.plan.section ||
                            it.plan.pos16 != (it.plan.phraseBar & 15);
                    disorder += v < lastPlan;
                    lastPlan = v, ++plans;
                }
            }
            if (fin && !any) break;
        }
    });
    for (int i = 0; i < kPushes; ++i)
    {
        Item e{};
        e.kind = Item::Event, e.ev.param = (int16_t)(i & 0xFFFF), e.ev.from = (int16_t)(i >> 16);
        if (events) r.push(e);
        if (i % 7 == 0)
        {
            Item pl{};
            const int v = i;   // (24 bits: section, phraseBar, piece; increasing)
            pl.kind = Item::Plan, pl.plan.section = (uint8_t)(v & 0xFF), pl.plan.phraseBar = (uint8_t)(v >> 8 & 0xFF);
            pl.plan.piece = (uint8_t)(v >> 16), pl.plan.slot[7].mask = (uint16_t)(v & 0xFFFF);
            pl.plan.activity[4] = pl.plan.section, pl.plan.pos16 = (uint8_t)(pl.plan.phraseBar & 15);
            r.push(pl);
        }
    }
    done.store(true, std::memory_order_release);
    consumer.join();
    const int lastPushed = (kPushes - 1) / 7 * 7;
    CHECK(disorder == 0 && torn == 0, "two threads: %d out of order, %d torn Plans", disorder, torn);
    CHECK(popped + r.dropped() == (events ? kPushes : 0), "two threads: %d popped + %d dropped, not %d", popped, r.dropped(), kPushes);
    CHECK(lastPlan == lastPushed && plans > 0, "two threads: the last Plan %d, not %d (%d Plans)", lastPlan, lastPushed, plans);
}

// The SKY log's lines, their serials and the audio, over a Next chord, with Link On or Off.
std::vector<std::string> skyRun(bool link, uint64_t& hash)
{
    auto p = make();
    if (link) setLink(*p, true);
    audioHash = 1469598103934665603ull;
    std::vector<std::string> lines;
    run(*p, 2.0);
    tp::app::nextChord();
    for (long done = 0; done < 3 * 44100; done += 128)
    {
        block(*p);
        std::string s;
        for (int l = 0; l < 3; ++l)
            s += std::to_string(p->sky().logSerial(l)) + ":" + p->sky().logText(l) + "|";
        if (lines.empty() || lines.back() != s) lines.push_back(s);
    }
    hash = audioHash;
    return lines;
}

void sky_log_unchanged()
{
    uint64_t hOff = 0, hOn = 0;
    const std::vector<std::string> off = skyRun(false, hOff), on = skyRun(true, hOn);
    bool nextShown = false;
    for (const std::string& s : off)
        nextShown |= s.find("Next chord") != std::string::npos;
    CHECK(nextShown, "sky: the log never showed the Next chord (the test proves nothing)");
    CHECK(off == on, "sky: the log differs with Link On (%d vs %d states)", (int)off.size(), (int)on.size());
    CHECK(hOff == hOn, "sky: the audio differs with Link On");
}

// The plugin's own link thread (everything above keeps it idle and pops the ring itself): made with the plugin, idle
// with Link Off, the ring's consumer with Link On (here with or without a sequencer to send to: tests/
// test_link_worker.cpp has the sending), asked for nothing from the audio thread but a flag, and joined with the plugin.
void worker_takes_the_ring()
{
    using tp::link::Worker;
    auto until = [](ThirdPlanet& p, auto done) {   // (blocks go on meanwhile, as on the MPC; 4 s at most)
        for (int i = 0; i < 800 && !done(); ++i)
        {
            block(p);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return done();
    };
    auto p = make(true);
    Worker& w = p->linkWorker_;
    run(*p, 0.3);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(w.state.load() == Worker::Off && w.takenCount.load() == 0, "worker: at work with Link Off");
    setLink(*p, true);
    CHECK(until(*p, [&] { return w.takenCount.load() >= 1; }), "worker: Link On, and the worker took no Plan");
    CHECK(w.state.load() != Worker::Off, "worker: Link On, and the link is not open");
    // (the ring is never popped here: the worker is its one consumer)
    setLink(*p, false);
    CHECK(until(*p, [&] { return w.state.load() == Worker::Off; }), "worker: Link Off, and the link is still open");
    const uint32_t taken = w.takenCount.load();
    for (int i = 0; i < 40; ++i)   // (200 ms: two of the idle thread's sleeps)
    {
        block(*p);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(w.takenCount.load() == taken && w.state.load() == Worker::Off, "worker: at work with Link Off again");
    setLink(*p, true);   // (on again: a Plan again, although nothing changed)
    CHECK(until(*p, [&] { return w.takenCount.load() > taken; }), "worker: Link On again, and no Plan");
    CHECK(p->linkRing_.dropped() == 0, "worker: Events dropped");
    p.reset();   // (with the link on: the thread is joined)
    // an instance that never played: the thread is made and joined all the same
    for (int i = 0; i < 20; ++i)
        ThirdPlanet q;
}


// ---- the computer's commands (spec 5.6), written as the worker writes them ----
void fade(ThirdPlanet& p, int level, int bars)
{
    p.linkWorker_.postFade((uint32_t)level, (uint32_t)bars);
}
void holdCommand(ThirdPlanet& p, bool on) { p.linkWorker_.holdCmd.store(on ? 1 : 0, std::memory_order_release); }
void setPlaying(bool on)
{
    if (on) ti.flags |= kVstTransportPlaying;
    else ti.flags &= ~kVstTransportPlaying;
}
// (the output of a run, block by block, from a point on)
void record(ThirdPlanet& p, long frames, std::vector<float>& out)
{
    for (long done = 0; done < frames; done += 128)
    {
        block(p);
        out.insert(out.end(), L, L + 128);
        out.insert(out.end(), R, R + 128);
    }
}
double rms(const std::vector<float>& v, long from, long n)   // (frames from..from+n of an interleaved-by-block record)
{
    double s = 0;
    long k = 0;
    for (long f = from; f < from + n; ++f)
    {
        const long b = f / 128, i = f % 128;
        if (b * 256 + 128 + i >= (long)v.size()) break;
        const double l = v[b * 256 + i], r = v[b * 256 + 128 + i];
        s += l * l + r * r, k += 2;
    }
    return k ? std::sqrt(s / k) : 0.0;
}
bool silent(const float* x, int n)
{
    for (int i = 0; i < n; ++i)
        if (x[i] != 0.0f) return false;
    return true;
}

// FADE(0, 4) at 120 BPM, playing: 4 bars are 8 s. The cos curve from 1 toward 0 is 0.92 one bar in and reaches 0 at
// 4 bars exactly; from there the output is silent. (Against a twin run with no fade: the music's own level moves.)
void fade_reaches_target_in_bars()
{
    const long bar = 2 * 44100, lead = 2 * 44100 / 128 * 128;
    std::vector<float> ref, out;
    for (int twin = 0; twin < 2; ++twin)
    {
        auto p = make();
        setLink(*p, true);
        setPlaying(true);
        run(*p, (double)lead / 44100);
        if (twin) fade(*p, 0, 4);   // (taken at the start of the next block)
        record(*p, 4 * bar + 44100, twin ? out : ref);
        drain(*p);
    }
    const long w = 2205;   // 50 ms
    const double ratio1 = rms(out, bar - w, 2 * w) / rms(ref, bar - w, 2 * w);
    CHECK(ratio1 > 0.5 && ratio1 < 0.95, "fade_reaches_target: one bar in, %.3f of the level without the fade", ratio1);
    const double before = rms(out, 4 * bar - w, w) / rms(ref, 4 * bar - w, w);
    CHECK(before < 0.02, "fade_reaches_target: the last 50 ms before 4 bars at %.4f of the level", before);
    const double after = rms(out, 4 * bar, w);
    CHECK(after <= 1e-6 && rms(ref, 4 * bar, w) > 1e-4, "fade_reaches_target: 50 ms from 4 bars at RMS %g", after);
    std::printf("fade_reaches_target: one bar in %.3f, the 50 ms before 4 bars %.4f, from 4 bars RMS %g\n", ratio1, before, after);
}

// FADE(0, 0) at once; a transport START brings the level back (the next set begins at full); with Link Off nothing.
void fade_zero_bars_then_start_resets()
{
    auto p = make();   // (Sync off: the piece plays on its own clock with the MPC stopped)
    setLink(*p, true);
    run(*p, 2.0);
    CHECK(!silent(L, 128), "fade_zero_bars: silent before the fade (the test proves nothing)");
    fade(*p, 0, 0);
    block(*p);
    CHECK(silent(L, 128) && silent(R, 128), "fade_zero_bars: the block after FADE(0, 0) is not silent");
    CHECK(p->linkGain() == 0.0f, "fade_zero_bars: gain %g, not 0", p->linkGain());
    run(*p, 0.5);
    CHECK(silent(L, 128), "fade_zero_bars: sound again with no START");
    setPlaying(true);
    block(*p);
    CHECK(p->linkGain() == 1.0f, "fade_zero_bars: gain %g a block after START, not 1", p->linkGain());
    bool sound = !silent(L, 128);
    for (long done = 0; done < 4 * 44100 && !sound; done += 128)
    {
        block(*p);
        sound = !silent(L, 128);
    }
    CHECK(sound, "fade_zero_bars: no sound within 2 bars of START");
    // with Sync on, the piece waits for the MPC: START resets the level at once, the piece follows on its bar line
    setPlaying(false);
    p->setNormalised(idx("sync"), 1.0f);
    run(*p, 1.0);
    fade(*p, 64, 0);
    block(*p);
    CHECK(std::fabs(p->linkGain() - 64.0f / 127.0f) < 1e-6f, "fade_zero_bars: FADE(64, 0) gain %g", p->linkGain());
    ti.ppqPos = 0.0;
    setPlaying(true);
    block(*p);
    CHECK(p->linkGain() == 1.0f, "fade_zero_bars: Sync on, gain %g a block after START", p->linkGain());
    sound = false;
    for (long done = 0; done < 4 * 44100 && !sound; done += 128)
    {
        block(*p);
        sound = !silent(L, 128);
    }
    CHECK(sound, "fade_zero_bars: Sync on, no sound within 2 bars of START");
    drain(*p);
}

// HOLD(1) through the hand's own path: the engine holds and the MPC's Hold parameter says On.
void hold_command_sets_hold()
{
    auto p = make();
    setLink(*p, true);
    run(*p, 1.0);
    CHECK(!tp::app::held(), "hold_command: held before the command");
    holdCommand(*p, true);
    block(*p), block(*p);
    CHECK(tp::app::held(), "hold_command: not held 2 blocks after HOLD(1)");
    run(*p, 0.1);
    CHECK(p->getBool(idx("hold")), "hold_command: the Hold parameter is not On");
    CHECK(p->linkWorker_.holdCmd.load() == -1, "hold_command: the command was not taken");
    holdCommand(*p, false);
    run(*p, 0.1);
    CHECK(!tp::app::held() && !p->getBool(idx("hold")), "hold_command: HOLD(0) did not release");
    drain(*p);
}

// START releases a hold the computer set, never one set by hand (nor one the hand already had when the command came).
void start_releases_link_hold_only()
{
    auto p = make();
    setLink(*p, true);
    run(*p, 1.0);
    holdCommand(*p, true);
    run(*p, 0.1);
    CHECK(tp::app::held(), "start_releases: the command did not hold");
    setPlaying(true);
    run(*p, 0.1);
    CHECK(!tp::app::held(), "start_releases: a hold set by command stays after START");
    run(*p, 0.1);
    CHECK(!p->getBool(idx("hold")), "start_releases: the Hold parameter still On");
    // by hand
    setPlaying(false);
    p->setNormalised(idx("hold"), 1.0f);
    run(*p, 0.1);
    CHECK(tp::app::held(), "start_releases: the Hold parameter did not hold");
    setPlaying(true);
    run(*p, 0.2);
    CHECK(tp::app::held() && p->getBool(idx("hold")), "start_releases: a hold set by hand released at START");
    // by hand, then the command on top: still the hand's
    setPlaying(false);
    holdCommand(*p, true);
    run(*p, 0.1);
    setPlaying(true);
    run(*p, 0.2);
    CHECK(tp::app::held(), "start_releases: a hand's hold released at START after a HOLD(1) on top");
    // by command, then the hand writes Hold On over it: the hand's now
    p->setNormalised(idx("hold"), 0.0f);
    run(*p, 0.1);
    setPlaying(false);
    holdCommand(*p, true);
    run(*p, 0.1);
    CHECK(tp::app::held(), "start_releases: the second command did not hold");
    p->setNormalised(idx("hold"), 0.0f);   // (the hand off and on again, through the parameter)
    run(*p, 0.1);
    p->setNormalised(idx("hold"), 1.0f);
    run(*p, 0.1);
    setPlaying(true);
    run(*p, 0.2);
    CHECK(tp::app::held(), "start_releases: the hand's hold (taken over from a command's) released at START");
    drain(*p);
}

// Link Off: commands are taken and dropped, never applied later; turning Link Off undoes what they did.
void link_off_ignores_commands()
{
    auto p = make();
    run(*p, 1.0);
    fade(*p, 0, 0);
    holdCommand(*p, true);
    run(*p, 0.2);
    CHECK(p->linkGain() == 1.0f && !silent(L, 128), "link_off: FADE applied with Link Off");
    CHECK(!tp::app::held(), "link_off: HOLD applied with Link Off");
    CHECK(p->linkWorker_.holdCmd.load() == -1, "link_off: the HOLD was not drained");
    setLink(*p, true);
    run(*p, 0.2);
    CHECK(p->linkGain() == 1.0f && !tp::app::held(), "link_off: a command from Link Off applied at Link On");
    fade(*p, 0, 0);
    holdCommand(*p, true);
    run(*p, 0.2);
    CHECK(p->linkGain() == 0.0f && tp::app::held(), "link_off: the commands not applied with Link On");
    setLink(*p, false);
    run(*p, 0.2);
    CHECK(p->linkGain() == 1.0f && !silent(L, 128), "link_off: the level not restored at Link Off");
    CHECK(!tp::app::held(), "link_off: the command's hold not released at Link Off");
    // a ramp under way, then Off: full at once
    setLink(*p, true);
    fade(*p, 0, 8);
    run(*p, 1.0);
    CHECK(p->linkGain() < 1.0f && p->linkGain() > 0.0f, "link_off: no ramp under way (%g)", p->linkGain());
    setLink(*p, false);
    block(*p);
    CHECK(p->linkGain() == 1.0f, "link_off: a ramp goes on with Link Off (%g)", p->linkGain());
    drain(*p);
}

// A hold the computer's HOLD set or released (and the release at START) is not sent back to it as the hand's EVENT:
// the PLAN's held flag shows it. The SKY log still says it, and the hand's own Hold is still an EVENT.
int holdEvents(const std::vector<Item>& v, int to = -1)
{
    int n = 0;
    for (const Item& it : v)
        n += it.kind == Item::Event && it.ev.who == tp::app::kWhoYou && it.ev.what == tp::app::kEvHold &&
             (to < 0 || it.ev.to == to);
    return n;
}
bool lastPlanHeld(const std::vector<Item>& v, bool& held)
{
    bool any = false;
    for (const Item& it : v)
        if (it.kind == Item::Plan) any = true, held = it.plan.held;
    return any;
}
void hold_command_not_echoed()
{
    auto p = make();
    setLink(*p, true);
    run(*p, 1.0);
    drain(*p);
    bool held = false;
    holdCommand(*p, true);
    run(*p, 0.1);
    std::vector<Item> v = drain(*p);
    CHECK(tp::app::held(), "hold_not_echoed: the command did not hold");
    CHECK(holdEvents(v) == 0, "hold_not_echoed: HOLD(1) came back as %d EVENTs", holdEvents(v));
    CHECK(lastPlanHeld(v, held) && held, "hold_not_echoed: no PLAN with held after HOLD(1)");
    CHECK(std::string(p->sky().logText(0)).find("Hold") != std::string::npos, "hold_not_echoed: the SKY log says \"%s\", not the hold",
          p->sky().logText(0));
    holdCommand(*p, false);
    run(*p, 0.1);
    v = drain(*p);
    CHECK(!tp::app::held(), "hold_not_echoed: HOLD(0) did not release");
    CHECK(holdEvents(v) == 0, "hold_not_echoed: HOLD(0) came back as %d EVENTs", holdEvents(v));
    CHECK(lastPlanHeld(v, held) && !held, "hold_not_echoed: no PLAN without held after HOLD(0)");
    // HOLD(0) with nothing held does nothing, and the hand's Hold right after it is still an EVENT
    holdCommand(*p, false);
    block(*p);
    p->setNormalised(idx("hold"), 1.0f);
    run(*p, 0.1);
    v = drain(*p);
    CHECK(holdEvents(v, 1) == 1 && holdEvents(v) == 1, "hold_not_echoed: the hand's Hold after a HOLD(0): %d EVENTs, not 1", holdEvents(v));
    p->setNormalised(idx("hold"), 0.0f);
    run(*p, 0.1);
    v = drain(*p);
    CHECK(holdEvents(v, 0) == 1 && holdEvents(v) == 1, "hold_not_echoed: the hand's Hold off: %d EVENTs, not 1", holdEvents(v));
    // the hand's Hold in the block after the command's: one EVENT, the hand's
    holdCommand(*p, true);
    block(*p);
    p->setNormalised(idx("hold"), 0.0f);
    run(*p, 0.1);
    v = drain(*p);
    CHECK(holdEvents(v, 0) == 1 && holdEvents(v) == 1, "hold_not_echoed: the hand's Hold off a block after HOLD(1): %d EVENTs, not 1",
          holdEvents(v));
    // the release at START of a hold the command set: no EVENT; then the hand's, an EVENT again
    holdCommand(*p, true);
    run(*p, 0.1);
    drain(*p);
    setPlaying(true);
    run(*p, 0.1);
    v = drain(*p);
    CHECK(!tp::app::held(), "hold_not_echoed: START did not release the command's hold");
    CHECK(holdEvents(v) == 0, "hold_not_echoed: the release at START came back as %d EVENTs", holdEvents(v));
    CHECK(lastPlanHeld(v, held) && !held, "hold_not_echoed: no PLAN without held after START");
    p->setNormalised(idx("hold"), 1.0f);
    run(*p, 0.1);
    v = drain(*p);
    CHECK(holdEvents(v, 1) == 1 && holdEvents(v) == 1, "hold_not_echoed: the hand's Hold after START: %d EVENTs, not 1", holdEvents(v));
    setPlaying(false);
    tp::app::setHold(false);
}

// A FADE that reached its level does not outlive the project: a project load (the FM-1 from power-on) is at full level.
void fade_reset_on_project_load()
{
    auto p = make();
    setLink(*p, true);
    run(*p, 1.0);
    fade(*p, 0, 0);
    run(*p, 0.2);
    CHECK(p->linkGain() == 0.0f && silent(L, 128), "fade_load: FADE(0, 0) did not silence (the test proves nothing)");
    const std::vector<uint8_t> state = p->internalSaveState();
    CHECK(p->internalLoadState(state.data(), state.size()), "fade_load: the project did not load");
    block(*p);
    CHECK(p->linkGain() == 1.0f, "fade_load: gain %g after a project load, not 1", p->linkGain());
    bool sound = false;
    for (long done = 0; done < 4 * 44100 && !sound; done += 128)
    {
        block(*p);
        sound = !silent(L, 128);
    }
    CHECK(sound, "fade_load: no sound within 4 s of the load");
    // the same FADE a second time is a FADE again (the worker's word carries a count)
    fade(*p, 64, 0);
    block(*p);
    CHECK(std::fabs(p->linkGain() - 64.0f / 127.0f) < 1e-6f, "fade_load: FADE(64, 0) gain %g", p->linkGain());
    CHECK(p->internalLoadState(state.data(), state.size()), "fade_load: the project did not load (1)");
    block(*p);
    CHECK(p->linkGain() == 1.0f, "fade_load: gain %g after the second load", p->linkGain());
    fade(*p, 64, 0);
    block(*p);
    CHECK(std::fabs(p->linkGain() - 64.0f / 127.0f) < 1e-6f, "fade_load: the same FADE again did nothing (gain %g)", p->linkGain());
    fade(*p, 127, 0);
    block(*p);
    // a ramp under way goes with the load too
    fade(*p, 0, 8);
    run(*p, 1.0);
    CHECK(p->linkGain() < 1.0f && p->linkGain() > 0.0f, "fade_load: no ramp under way (%g)", p->linkGain());
    CHECK(p->internalLoadState(state.data(), state.size()), "fade_load: the project did not load (2)");
    block(*p);
    CHECK(p->linkGain() == 1.0f, "fade_load: a ramp goes on after a project load (%g)", p->linkGain());
    run(*p, 0.5);
    CHECK(p->linkGain() == 1.0f, "fade_load: the ramp came back after the load (%g)", p->linkGain());
    drain(*p);
}

// SysEx delivered to the plugin: Harmony Link's own (an echo) and another maker's change nothing (the SDK hands the
// plugin no SysEx at all; a 3-byte event with status F0 is not a note).
void own_sysex_ignored()
{
    static char own[] = {(char)0xF0, 0x7D, 0x54, 0x50, 0x01, 0x01, 0x00, 0x00, (char)0xF7};
    static char other[] = {(char)0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, (char)0xF7};
    auto sysex = [](ThirdPlanet& p, char* data, int n) {
        VstMidiSysexEvent sx{};
        sx.type = kVstSysExType, sx.byteSize = sizeof sx, sx.dumpBytes = n, sx.sysexDump = data;
        VstMidiEvent m{};   // (a host that cuts SysEx into short events)
        m.type = kVstMidiType, m.byteSize = sizeof m;
        m.midiData[0] = data[0], m.midiData[1] = data[1], m.midiData[2] = data[2];
        VstEvents ev{};
        ev.numEvents = 2;
        ev.events[0] = reinterpret_cast<VstEvent*>(&sx);
        ev.events[1] = reinterpret_cast<VstEvent*>(&m);
        p.internalAddMidi(&ev);
    };
    uint64_t h[3];
    bool heldAfter[3];
    for (int k = 0; k < 3; ++k)
    {
        auto p = make();
        setLink(*p, true);
        audioHash = 1469598103934665603ull;
        run(*p, 1.0);
        for (int i = 0; i < 20; ++i)
        {
            if (k == 1) sysex(*p, own, sizeof own);
            if (k == 2) sysex(*p, other, sizeof other);
            block(*p);
        }
        run(*p, 1.0);
        h[k] = audioHash, heldAfter[k] = tp::app::held();
        drain(*p);
    }
    CHECK(h[1] == h[0] && heldAfter[1] == heldAfter[0], "own_sysex: Harmony Link's SysEx changed the plugin");
    CHECK(h[2] == h[0] && heldAfter[2] == heldAfter[0], "own_sysex: another maker's SysEx changed the plugin");
}

} // namespace

int main()
{
    worker_takes_the_ring();
    no_items_when_link_off();
    plan_on_change_only();
    next_chord_forwards_event_then_plan();
    evolve_params_not_forwarded();
    chord_mask_written_slot();
    capture_reads_readouts();
    ring_order_and_capacity();
    ring_overflow_keeps_latest_plan();
    ring_two_threads(true);
    ring_two_threads(false);
    sky_log_unchanged();
    fade_reaches_target_in_bars();
    fade_zero_bars_then_start_resets();
    hold_command_sets_hold();
    start_releases_link_hold_only();
    link_off_ignores_commands();
    hold_command_not_echoed();
    fade_reset_on_project_load();
    own_sysex_ignored();
    std::printf(fails ? "FAILED (%d)\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
