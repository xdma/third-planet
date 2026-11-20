// SPDX-License-Identifier: GPL-3.0-only
// Third Planet - an MPC instrument plugin by Shostak.
//
// A port of NACHTFAHRT by Cranky Man (https://github.com/FartyPants/fm1-nachtfahrt), the generative
// 1970s Berlin School firmware for the M-VAVE FM-1. Both the sound engine and the FM-1's whole app
// (its pages, buttons, keys, PERFORM, pieces, Evolve) are NACHTFAHRT's own code, unchanged
// (src/nachtfahrt/, docs/VENDOR.md). This file is the FM-1's panel, made of MPC controls:
//
//   MIDI notes F3..G5 (53..79)   the FM-1's 27 keys: white keys play the Lead in the scale, black keys
//                                hold the effects; on PERFORM, Pattern and Chords they do what they do there
//   MIDI notes C2..C#3 (36..49)  the FM-1's 14 buttons, held while the note is held:
//                                FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+
//   other MIDI notes             the FM-1's MIDI in: the Lead, to the nearest note of the scale
//   parameters                   every FM-1 knob, absolute (src/params.h)
//   Sync                         the MPC's tempo and transport, sent to the FM-1 as MIDI clock
//
// Signal flow: the panel -> the FM-1 app (ui.c) -> engine commands -> en_render (Drone, Sequence,
// Sequence 2, Lead, Atmosphere, echo, plate) in 256-frame blocks -> stereo out. Time inside is the
// FM-1's, counted in rendered audio (app_unit.cpp), so nothing depends on the MPC's block size. The
// instrument plays by itself from the moment it is loaded, as the FM-1 does from power-on.
//
// One engine per process (its state is C globals): the first instance to process owns it; any other
// stays silent until the owner is removed. The project (every piece as it was left) is saved with the
// MPC project.

#include "mpc_plugin.h"

#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app_api.h"
#include "engine.h"
#include "params.h"

namespace {

constexpr int kKeyNote0 = 53;      // F3: the FM-1's key 0
constexpr int kButtonNote0 = 36;   // C2: the FM-1's first button (FX)
constexpr int kMaxSends = 24;      // parameters sent back to the MPC per block (Kolo's device finding)
constexpr int kSettleChunks = 64;  // ~0.37 s after a boot, every value has been sent back to the MPC

// Display parameters: what the screen's status line shows, appended after the controls. Outputs only:
// not automatable, never read by the sound. A blank value is " " (an empty string shows the number).
constexpr int kDispPage = tp::kNumControls;   // the FM-1's page; one more value: another instance plays
constexpr int kDispChord = kDispPage + 1;     // the chord sounding (tp::app::chordCode)
constexpr int kDispPerform = kDispPage + 2;   // PERFORM and its Lock
constexpr int kDispHold = kDispPage + 3;      // REC's Hold
constexpr int kNumDisplay = 4;
static_assert(kDispPerform == kDispChord + 1 && kDispHold == kDispPage + kNumDisplay - 1, "status() fills them in this order");
const char* const kPerformText[] = {" ", "PERFORM", "PERFORM  LOCK", "LOCK"};
const char* const kHoldText[] = {" ", "HOLD"};
const char* const kBusyText = "Another Third Planet plays";
constexpr int kFixFrames = 4410;   // a status value the MPC wrote over is corrected at most every 100 ms

const char* const kKeyModes[] = {"Panel", "Lead"};
const char* const kOffOn[] = {"Off", "On"};

// A value as the FM-1's screen shows it.
void label(const tp::ParamDef& d, int v, char* buf, size_t n)
{
    using namespace tp::app;
    switch (d.kind)
    {
        case tp::Kind::Master: std::snprintf(buf, n, "%d%%", v); return;
        case tp::Kind::Piece: std::snprintf(buf, n, "%s", en_piece_name(v)); return;
        case tp::Kind::Walk: std::snprintf(buf, n, "%s", EN_WALK_NAME[v % EN_NWALK]); return;
        case tp::Kind::Sync: std::snprintf(buf, n, "%s", kOffOn[v ? 1 : 0]); return;
        case tp::Kind::KeyMode: std::snprintf(buf, n, "%s", kKeyModes[v ? 1 : 0]); return;
        case tp::Kind::Voice: break;
        case tp::Kind::Knob: break;
    }
    const int c = d.code;
    if (c < P_NPARAMS)
    {
        char t[16];
        en_param_text(c, v, t);
        std::snprintf(buf, n, "%s", t[0] == '+' ? t + 1 : t);   // (the FM-1 drops a leading '+')
        return;
    }
    switch (c)
    {
        case kKnobFilter:
            if (v == 0) std::snprintf(buf, n, "Off");
            else std::snprintf(buf, n, "%s %d", v < 0 ? "Shut" : "Open", std::abs(v) * 2);
            return;
        case kKnobWet: std::snprintf(buf, n, "%+d", v * 2); return;
        case kKnobEnergy: std::snprintf(buf, n, "%+d", v); return;
        case kKnobMode: std::snprintf(buf, n, "%s", EN_MODE_NAME[modeAt(v)]); return;
        case kKnobLoop: std::snprintf(buf, n, "%s", v ? "Loop" : "Endless"); return;
        case kKnobSlot: std::snprintf(buf, n, "%d", v + 1); return;
        case kKnobChord: std::snprintf(buf, n, "%s", EN_CQ_NAME[v % EN_NCQ]); return;
        case kKnobType: std::snprintf(buf, n, "%s", EN_CX_NAME[v % EN_NCX]); return;
        case kKnobLane: std::snprintf(buf, n, "%s", laneName(v)); return;
        default: std::snprintf(buf, n, "%d", v); return;
    }
}

int rangeLo(const tp::ParamDef& d)
{
    using namespace tp::app;
    if (d.kind != tp::Kind::Knob && d.kind != tp::Kind::Voice) return 0;
    if (d.code < P_NPARAMS) return en_param_info(d.code)->lo;
    return d.code >= kKnobFilter && d.code <= kKnobSpace ? -50 : 0;
}

int rangeHi(const tp::ParamDef& d)
{
    using namespace tp::app;
    switch (d.kind)
    {
        case tp::Kind::Master: return 200;
        case tp::Kind::Piece: return EN_NPIECES - 1;
        case tp::Kind::Walk: return EN_NWALK - 1;
        case tp::Kind::Sync: return 1;
        case tp::Kind::KeyMode: return 1;
        case tp::Kind::Voice:
        case tp::Kind::Knob: break;
    }
    if (d.code < P_NPARAMS) return en_param_info(d.code)->hi;
    switch (d.code)
    {
        case kKnobFilter: case kKnobWet: case kKnobEnergy: case kKnobSpace: return 50;
        case kKnobMode: return EN_NMODES - 1;
        case kKnobLoop: return 1;
        case kKnobSlot: return EN_NPROG - 1;
        case kKnobChord: return EN_NCQ - 1;
        case kKnobType: return EN_NCX - 1;
        case kKnobLane: return EN_NDRUM - 1;
        case kKnobLaneChance: return 100;
        default: return 0;
    }
}

int rangeDef(const tp::ParamDef& d)
{
    if (d.kind == tp::Kind::Master) return 100;
    if ((d.kind == tp::Kind::Knob || d.kind == tp::Kind::Voice) && d.code < P_NPARAMS) return en_param_info(d.code)->def;
    return 0;
}

// The MPC's parameter for an FM-1 control: a list when it has few named values, else a number.
mpc::Param makeParam(const tp::ParamDef& d)
{
    const char* name = d.name ? d.name : en_param_info(d.code)->name;
    const int lo = rangeLo(d), hi = rangeHi(d), def = rangeDef(d);
    if (d.kind == tp::Kind::Sync) return mpc::boolParam(d.id, name, false);
    if (hi - lo < 25 && d.kind != tp::Kind::Master)
    {
        std::vector<std::string> names;
        char buf[32];
        for (int v = lo; v <= hi; ++v)
        {
            label(d, v, buf, sizeof buf);
            names.emplace_back(buf);
        }
        mpc::Param p = mpc::choiceParam(d.id, name, names, def - lo);
        return p;
    }
    return mpc::intParam(d.id, name, lo, hi, def, "");
}

} // namespace

class ThirdPlanet : public mpc::Plugin
{
public:
    ThirdPlanet() : mpc::Plugin(makeInfo())
    {
        for (int i = 0; i < tp::kNumControls; ++i)
        {
            addParam(makeParam(tp::kParams[i]));
            lo_[i] = rangeLo(tp::kParams[i]);
            if (tp::kParams[i].kind == tp::Kind::Sync) idxSync_ = i;
            if (tp::kParams[i].kind == tp::Kind::KeyMode) idxKeyMode_ = i;
            appVal_[i].store(INT_MIN, std::memory_order_relaxed);
            dirty_[i].store(false, std::memory_order_relaxed);
        }
        std::vector<std::string> pages{" "};
        for (int v = 0; v < tp::app::numViews(); ++v) pages.emplace_back(tp::app::viewName(v));
        pages.emplace_back(kBusyText);
        addDisplay(mpc::choiceParam("d_page", "Page", pages, 0));
        addDisplay(mpc::intParam("d_chord", "Chord", 0, tp::app::kChordCodes - 1, 0, ""));
        addDisplay(mpc::choiceParam("d_perform", "Perform", std::vector<std::string>(kPerformText, kPerformText + 4), 0));
        addDisplay(mpc::choiceParam("d_hold", "Hold", std::vector<std::string>(kHoldText, kHoldText + 2), 0));
        displayReset_.store(true, std::memory_order_relaxed);
    }

    ~ThirdPlanet() override { lease_.release(this); }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)sampleRate;   // the engine runs at 44100 Hz, the MPC's fixed rate
        (void)maxBlockSize;
    }

    // A value from the MPC (a Q-Link, the screen, automation, a project load). Applied on the audio
    // thread before the next block, in parameter order. A value the app itself just sent is not sent back.
    void parameterChanged(int index, float plainValue) override
    {
        if (index >= kDispPage && index < kDispPage + kNumDisplay)
        {   // (the MPC wrote a status value itself, a load or automation: the true one goes out again)
            if ((int)std::lround(plainValue) != displaySent_[index - kDispPage].load(std::memory_order_acquire))
                displayFix_.store(true, std::memory_order_release);
            return;
        }
        if (index < 0 || index >= tp::kNumControls) return;
        const int v = (int)std::lround(plainValue) + (isChoice(index) ? lo_[index] : 0);
        if (v == appVal_[index].load(std::memory_order_acquire)) return;
        wanted_[index].store(v, std::memory_order_relaxed);
        dirty_[index].store(true, std::memory_order_release);
    }

    bool formatValue(int index, float plainValue, std::string& out) override
    {
        if (index == kDispChord)
        {
            char t[tp::app::kChordTextMax];
            tp::app::chordText(std::isfinite(plainValue) ? (int)std::lround(plainValue) : 0, t);
            out = t[0] != 0 ? t : " ";
            return true;
        }
        if (index < 0 || index >= tp::kNumControls || isChoice(index)) return false;
        char buf[32];
        label(tp::kParams[index], (int)std::lround(plainValue), buf, sizeof buf);
        out = buf;
        return true;
    }

    // The project: the FM-1's own save (every piece as it was left), as Third Planet last copied it.
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        uint8_t buf[tp::app::kStoreMax];
        int len = -1;
        if (lease_.owns(this) && booted_.load(std::memory_order_acquire))
        {
            for (int tries = 0; tries < 8; ++tries)
            {
                const uint32_t s0 = snapSeq_.load(std::memory_order_acquire);
                if (s0 & 1u) continue;
                const int k = (int)(s0 >> 1) & 1;
                len = snapLen_[k];
                if (len > 0) std::memcpy(buf, snap_[k], (size_t)len);
                if (snapSeq_.load(std::memory_order_acquire) == s0) break;
                len = -1;
            }
        }
        if (len <= 0)
        {
            len = ownLen_;
            if (len > 0) std::memcpy(buf, ownBlob_, (size_t)len);
        }
        out.clear();
        out.push_back('T'), out.push_back('P'), out.push_back('1'), out.push_back(0);
        if (len > 0) out.insert(out.end(), buf, buf + len);
    }

    void loadExtraState(const uint8_t* data, size_t size) override
    {
        if (size < 4 || std::memcmp(data, "TP1", 4) != 0) return;
        const size_t n = size - 4 <= (size_t)tp::app::kStoreMax ? size - 4 : 0;
        std::memcpy(pendingBlob_, data + 4, n);
        pendingLen_ = n > 0 ? (int)n : -1;
        hostKnows_.store(true, std::memory_order_release);
        displayReset_.store(true, std::memory_order_release);   // (the load brought the saved status values)
        loadPending_.store(true, std::memory_order_release);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        bool fresh = false;
        if (!lease_.claim(this, fresh))   // another instance owns the engine: silence, and the screen says so
        {
            const int busy[kNumDisplay] = {tp::app::numViews() + 1, 0, 0, 0};
            sendDisplay(busy, kMaxSends, ctx.numFrames);
            return;
        }
        if (loadPending_.exchange(false, std::memory_order_acq_rel))
        {
            ownLen_ = pendingLen_;
            if (ownLen_ > 0) std::memcpy(ownBlob_, pendingBlob_, (size_t)ownLen_);
            fresh = true;
        }
        if (fresh || !booted_.load(std::memory_order_relaxed)) boot();

        applyParams();
        tp::app::setMaster((uint32_t)getInt(0) * 2048u / 100u);
        midiIn(ctx);
        clock(ctx);
        render(ctx);
        mirror(kMaxSends - sendDisplay(status(), kMaxSends, ctx.numFrames));
        if (chunks_ > kSettleChunks) hostKnows_.store(true, std::memory_order_release);   // (every value sent back by now)
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "Third Planet";
        info.vendor = "Shostak";
        info.uniqueId = MPC_FOURCC('S', 's', 'n', 'i');
        info.version = 1000;
        info.isInstrument = true;
        info.numInputs = 0;
        info.numOutputs = 2;
        return info;
    }

    bool isChoice(int i) const { return param(i).kind == mpc::Param::Kind::Choice; }

    // The FM-1 from power-on, with this instance's project (or the recipes).
    void boot()
    {
        tp::app::storeWrite(ownLen_ > 0 ? ownBlob_ : nullptr, ownLen_ > 0 ? ownLen_ : -1);
        tp::app::boot();
        pos_ = tp::kChunk;
        chunks_ = 0;
        keys_ = buttons_ = 0;
        std::memset(noteCount_, 0, sizeof noteCount_);
        clockRunning_ = false;
        lastTick_ = LLONG_MIN;
        displayReset_.store(true, std::memory_order_relaxed);
        // The FM-1's project is the truth for everything it saves: values the MPC loaded alongside it are
        // dropped (as shown, they may carry Motion's octave shift, or a walk slot as if written). What the
        // FM-1 never saves comes back from the MPC once it holds this instance's values (a project
        // loaded, or values sent back earlier): the three macros, the Pattern lane, the chord slot.
        const bool restore = hostKnows_.load(std::memory_order_acquire);
        for (int i = 0; i < tp::kNumControls; ++i)
        {
            appVal_[i].store(INT_MIN, std::memory_order_relaxed);
            const tp::ParamDef& d = tp::kParams[i];
            if (d.kind != tp::Kind::Knob && d.kind != tp::Kind::Voice && d.kind != tp::Kind::Piece && d.kind != tp::Kind::Walk)
                continue;   // (Volume, Sync, Keys: the plugin's own, kept as they are)
            const bool unsaved = d.kind == tp::Kind::Knob &&
                                 (d.code == tp::app::kKnobFilter || d.code == tp::app::kKnobWet ||
                                  d.code == tp::app::kKnobEnergy || d.code == tp::app::kKnobLane ||
                                  d.code == tp::app::kKnobSlot);
            if (restore && unsaved)
            {
                wanted_[i].store((int)std::lround(get(i)) + (isChoice(i) ? lo_[i] : 0), std::memory_order_relaxed);
                dirty_[i].store(true, std::memory_order_release);
            }
            else
                dirty_[i].store(false, std::memory_order_release);
        }
        booted_.store(true, std::memory_order_release);
    }

    void applyParams()
    {
        using namespace tp;
        for (int i = 0; i < kNumControls; ++i)
        {
            if (!dirty_[i].exchange(false, std::memory_order_acq_rel)) continue;
            const int v = wanted_[i].load(std::memory_order_relaxed);
            const ParamDef& d = kParams[i];
            switch (d.kind)
            {
                case Kind::Knob: app::knob(d.code, v); break;
                case Kind::Voice: app::voice(d.code, v); break;
                case Kind::Piece: app::setPiece(v); break;
                case Kind::Walk: app::walk(v); break;
                case Kind::Sync:
                    if (!v && syncWas_)   // back to the FM-1's own clock: the clock stops coming (the engine
                    {                     // drops it after 100 blocks) and the piece plays on, or starts if it rested
                        clockRunning_ = false;
                        unsyncPending_ = true;
                    }
                    syncWas_ = v != 0;
                    break;
                case Kind::Master:
                case Kind::KeyMode: break;   // read where used
            }
        }
    }

    // MIDI notes: the FM-1's keys, its buttons, or its MIDI in.
    void midiIn(const mpc::ProcessContext& ctx)
    {
        const bool leadOnly = getInt(idxKeyMode_) != 0;
        for (int i = 0; i < ctx.numMidiEvents; ++i)
        {
            const mpc::MidiEvent& e = ctx.midi[i];
            if (e.isAllNotesOff()) { allOff(); continue; }
            if (!e.isNoteOn() && !e.isNoteOff()) continue;
            const int n = e.note() & 127;
            const bool on = e.isNoteOn();
            if (on) { if (noteCount_[n] < 255) noteCount_[n]++; }
            else if (noteCount_[n] > 0) noteCount_[n]--;
            const bool down = noteCount_[n] > 0;
            if (!leadOnly && n >= kKeyNote0 && n < kKeyNote0 + tp::app::kNumKeys)
                keys_ = down ? keys_ | 1u << (n - kKeyNote0) : keys_ & ~(1u << (n - kKeyNote0));
            else if (!leadOnly && n >= kButtonNote0 && n < kButtonNote0 + tp::app::kNumButtons)
                buttons_ = down ? buttons_ | 1u << (n - kButtonNote0) : buttons_ & ~(1u << (n - kButtonNote0));
            else
                tp::app::midiIn(on ? 0x09u | 0x90u << 8 | (uint32_t)n << 16 | (uint32_t)e.velocity() << 24
                                   : 0x08u | 0x80u << 8 | (uint32_t)n << 16);
        }
        tp::app::setKeys(keys_);
        tp::app::setButtons(buttons_);
    }

    void allOff()
    {
        for (int n = 0; n < 128; ++n)
            if (noteCount_[n] && (getInt(idxKeyMode_) != 0 || ((n < kKeyNote0 || n >= kKeyNote0 + tp::app::kNumKeys) &&
                                                      (n < kButtonNote0 || n >= kButtonNote0 + tp::app::kNumButtons))))
                tp::app::midiIn(0x08u | 0x80u << 8 | (uint32_t)n << 16);
        std::memset(noteCount_, 0, sizeof noteCount_);
        keys_ = buttons_ = 0;
    }

    // Sync: the MPC's transport as the FM-1's MIDI clock in (24 a beat, start, stop).
    void clock(const mpc::ProcessContext& ctx)
    {
        if (unsyncPending_)
        {
            unsyncPending_ = false;
            tp::app::play(true);
        }
        if (!getBool(idxSync_)) { stoppedSent_ = false; return; }
        const mpc::Transport& t = ctx.transport;
        const bool run = t.playing && t.ppqValid;
        if (run && !clockRunning_)
        {
            tp::app::midiIn(0x0F | 0xFAu << 8);   // Start: the piece from the top, on the MPC's play
            clockRunning_ = true;
            lastTick_ = (long long)std::floor(t.ppqPos * 24.0) - 1;
        }
        else if (!run && clockRunning_)
        {
            tp::app::midiIn(0x0F | 0xFCu << 8);
            clockRunning_ = false;
        }
        else if (!run && !stoppedSent_)
        {
            tp::app::midiIn(0x0F | 0xFCu << 8);   // Sync on with the MPC stopped: rest until it plays
        }
        stoppedSent_ = !run;
        if (!run) return;
        const double end = t.ppqPos + (double)ctx.numFrames * t.bpm / 60.0 / 44100.0;
        long long last = (long long)std::floor(end * 24.0);
        if (last - lastTick_ > 48) lastTick_ = last - 48;   // (a jump in the song: no burst of clocks)
        for (; lastTick_ < last; ++lastTick_) tp::app::midiIn(0x0F | 0xF8u << 8);
    }

    // The FM-1's time runs on as audio is needed: each simulated millisecond renders a 256-frame block
    // when one falls due, then runs the app.
    void render(mpc::ProcessContext& ctx)
    {
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        int done = 0;
        while (done < ctx.numFrames)
        {
            if (pos_ >= tp::kChunk)
            {
                while (!tp::app::stepMs(buf_)) {}
                pos_ = 0;
                ++chunks_;
                snapshot();   // (every block: a save is never behind what plays)
            }
            int n = ctx.numFrames - done;
            if (n > tp::kChunk - pos_) n = tp::kChunk - pos_;
            for (int i = 0; i < n; ++i)
            {
                outL[done + i] = (float)buf_[2 * (pos_ + i)] * tp::kOutScale;
                if (outR != nullptr) outR[done + i] = (float)buf_[2 * (pos_ + i) + 1] * tp::kOutScale;
            }
            pos_ += n;
            done += n;
        }
    }

    // What the app changed by itself (a piece, Evolve, a shake, the engine's macros) back to the MPC.
    // The status line's values now.
    struct Status { int v[kNumDisplay]; operator const int*() const { return v; } };
    static Status status()
    {
        using namespace tp::app;
        return {{view() + 1, chordCode(), perform() ? (locked() ? 2 : 1) : (locked() ? 3 : 0), held() ? 1 : 0}};
    }

    // The status values that changed since they were last sent, to the MPC (it redraws only what it is
    // told of). Returns how many were sent, at most `budget`.
    // `frames`: the block's length, for the pace of corrections.
    int sendDisplay(const int* now, int budget, int frames)
    {
        sinceFix_ = sinceFix_ < kFixFrames ? sinceFix_ + frames : kFixFrames;
        bool reset = displayReset_.exchange(false, std::memory_order_acq_rel);
        if (sinceFix_ >= kFixFrames && displayFix_.exchange(false, std::memory_order_acq_rel))
        {   // (paced: should the MPC ever write back a rounded value, this must not run every block)
            reset = true;
            sinceFix_ = 0;
        }
        if (reset)
            for (std::atomic<int>& v : displaySent_) v.store(INT_MIN, std::memory_order_relaxed);
        int sent = 0;
        for (int k = 0; k < kNumDisplay && sent < budget; ++k)
        {
            if (now[k] == displaySent_[k].load(std::memory_order_relaxed)) continue;
            displaySent_[k].store(now[k], std::memory_order_release);
            setNormalised(kDispPage + k, param(kDispPage + k).toNormalised((float)now[k]));
            notifyHostParameterChanged(kDispPage + k);
            ++sent;
        }
        return sent;
    }

    void addDisplay(mpc::Param p)
    {
        p.automatable = false;
        addParam(p);
    }

    void mirror(int budget)
    {
        using namespace tp;
        int sent = 0;
        for (int k = 0; k < kNumControls && sent < budget; ++k)
        {
            const int i = (mirrorFrom_ + k) % kNumControls;
            const ParamDef& d = kParams[i];
            int v;
            switch (d.kind)
            {
                case Kind::Knob: v = app::knobValue(d.code); break;
                case Kind::Voice: v = app::par(d.code); break;
                case Kind::Piece: v = app::piece(); break;
                case Kind::Walk: v = app::walkValue(); break;
                default: continue;
            }
            if (v == appVal_[i].load(std::memory_order_relaxed) || dirty_[i].load(std::memory_order_acquire)) continue;
            appVal_[i].store(v, std::memory_order_release);
            setNormalised(i, param(i).toNormalised((float)(isChoice(i) ? v - lo_[i] : v)));
            notifyHostParameterChanged(i);
            ++sent;
            mirrorFrom_ = (i + 1) % kNumControls;
        }
    }

    void snapshot()
    {
        // A sequence count: even when stable, its half's parity names the latest copy. The writer fills the
        // other copy while the count is odd; a reader retries if the count moved while it copied.
        const uint32_t s0 = snapSeq_.load(std::memory_order_relaxed);
        const int k = 1 - ((int)(s0 >> 1) & 1);
        snapSeq_.store(s0 + 1, std::memory_order_release);
        snapLen_[k] = tp::app::snapshot(snap_[k], tp::app::kStoreMax);
        snapSeq_.store(s0 + 2, std::memory_order_release);
    }

    tp::EngineLease lease_;
    std::atomic<bool> booted_{false};
    std::atomic<bool> hostKnows_{false};   // the MPC's values are this instance's (not a new instance's defaults)

    // parameters: what the app last showed (sent back), and what the MPC asked for
    int lo_[tp::kNumControls] = {};
    std::atomic<int> appVal_[tp::kNumControls];
    std::atomic<int> wanted_[tp::kNumControls];
    std::atomic<bool> dirty_[tp::kNumControls];
    int mirrorFrom_ = 0;
    std::atomic<int> displaySent_[kNumDisplay] = {{INT_MIN}, {INT_MIN}, {INT_MIN}, {INT_MIN}};   // (INT_MIN: never)
    std::atomic<bool> displayReset_{true};   // send every status value again (a boot, a load)
    std::atomic<bool> displayFix_{false};    // the MPC wrote over a status value
    int sinceFix_ = kFixFrames;
    int idxSync_ = 0, idxKeyMode_ = 0;

    // the panel
    uint32_t keys_ = 0, buttons_ = 0;
    uint8_t noteCount_[128] = {};

    // Sync
    bool syncWas_ = false, unsyncPending_ = false, clockRunning_ = false, stoppedSent_ = false;
    long long lastTick_ = 0;

    // audio
    int32_t buf_[2 * tp::kChunk] = {};
    int pos_ = tp::kChunk;
    uint32_t chunks_ = 0;

    // the project: this instance's (loaded, or none), one waiting to load, and the copies for saving
    uint8_t ownBlob_[tp::app::kStoreMax] = {};
    int ownLen_ = -1;
    uint8_t pendingBlob_[tp::app::kStoreMax] = {};
    int pendingLen_ = -1;
    std::atomic<bool> loadPending_{false};
    uint8_t snap_[2][tp::app::kStoreMax] = {};
    int snapLen_[2] = {-1, -1};
    std::atomic<uint32_t> snapSeq_{0};
};

// This macro creates the exported VSTPluginMain entry point. Keep it last.
MPC_PLUGIN_ENTRY(ThirdPlanet)
