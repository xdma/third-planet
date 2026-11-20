// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: NACHTFAHRT's app layer (src/nachtfahrt/app/ui.c and project.c, by Cranky Man, unchanged) on a
// simulated FM-1, the way upstream's own host simulator (host/amb_host.c) runs it.
//
// ui.c is the FM-1's whole behaviour: the pages and knobs, what every button and key does on a tap, a
// hold or with another held, PERFORM, the Chords and Pattern pages, the pieces, Evolve's slow nudges,
// shakes, saving. Rather than re-implementing it, Third Planet runs it unchanged and feeds it a virtual
// panel (plat.h): held buttons and keys, encoder detents, the MASTER pot, USB-MIDI in, a flash store.
//
// Time is the FM-1's, derived from rendered audio, as in amb_host.c's run_ms(): each simulated
// millisecond first renders the 256-frame audio blocks that have fallen due, then runs the UI (a full
// frame every 16 ms, input only in between). The app therefore behaves the same whatever the MPC's block
// size, and the same as amb_host.c run on the same input.
//
// The FM-1's screen is not shown on the MPC: the drawing code runs against a blank font and an LCD that
// discards pixels. The screen reader (announce.c) is absent, as on an FM-1 with no reader attached.
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "app_api.h"
#include "power.h"

#pragma clang fp contract(off)   // as upstream builds the app (-ffp-contract=off)
#pragma clang diagnostic ignored "-Wunused-function"   // (upstream's dead code: toggle_note, tag, line ...)

extern "C" {

// ------------------------------------------------------------------ screen (discarded) ---
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x, (void)y, (void)w, (void)h, (void)c; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t* px) { (void)x, (void)y, (void)w, (void)h, (void)px; }
TP_SECTIONS_BEGIN   // every app variable, for power_on() (power.h)
#define __attribute__(x)   // gfx.c's device RAM section (as amb_host.c drops it)
#include "nachtfahrt/gfx.c"
#undef __attribute__
TP_SECTIONS_END

// ------------------------------------------------------------------ the virtual panel ---
#include "nachtfahrt/app/plat.h"

static uint32_t now_ms, held_btn, held_keys, master_q12 = 2048;
static int32_t enc_acc[NE];
static uint32_t lit_btn, lit_keys;
static int glow_on = 1;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return master_q12; }
void plat_leds(uint32_t b, uint32_t k) { lit_btn = b, lit_keys = k; }
void plat_glow(int on) { glow_on = on; }
void plat_play_red(int red) { (void)red; }

#define TP_MQ 256u
static uint32_t midi_q[TP_MQ], midi_w, midi_r;
int plat_midi_in(uint32_t* pkt)
{
    if (midi_r == midi_w) return 0;
    *pkt = midi_q[midi_r++ % TP_MQ];
    return 1;
}
void plat_midi_out(uint32_t pkt) { (void)pkt; }   // the MPC discards plugin MIDI out

static uint8_t store[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_len[OBJ_NOBJ] = {-1, -1};
static uint32_t store_gen;   // counts saves, so the plugin knows when to take a new copy
int plat_store_load(uint32_t obj, void* dst, uint32_t max)
{
    int n;
    if (obj >= OBJ_NOBJ || (n = store_len[obj]) < 0) return -1;
    if ((uint32_t)n > max) n = (int)max;
    memcpy(dst, store[obj], (size_t)n);
    return n;
}
int plat_store_save(uint32_t obj, const void* src, uint32_t len)
{
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX) return -1;
    memcpy(store[obj], src, len);
    store_len[obj] = (int)len;
    store_gen++;
    return 0;
}

uint32_t plat_cpu_pct(void) { return 0; }
uint32_t plat_xruns(void) { return 0; }
uint32_t plat_cpu_peak(void) { return 0; }
int32_t plat_battery(void) { return -1; }
int plat_usb_power(void) { return 1; }

// ------------------------------------------------------------------ the app's interface ---
#include "nachtfahrt/app/app.h"

// ------------------------------------------------------------------ no screen reader ---
#define ANN_MAX 240u
#define ANN_NOW 1u
#define ANN_KEEP 2u
static int ann_on(void) { return 0; }
static void ann_say(const char* a, const char* b, uint32_t flags) { (void)a, (void)b, (void)flags; }
static void ann_later(const char* s, uint32_t flags, uint32_t quiet_ms, uint32_t now) { (void)s, (void)flags, (void)quiet_ms, (void)now; }
static int ann_where(void) { return 0; }
static void ann_tick(uint32_t now) { (void)now; }

// ------------------------------------------------------------------ the app, unchanged ---
TP_SECTIONS_BEGIN
#include "nachtfahrt/app/project.c"
#include "nachtfahrt/app/ui.c"
TP_SECTIONS_END

} // extern "C"

// ------------------------------------------------------------------ the plugin's side ---
namespace tp {
namespace app {

static double audio_due_ms;
static uint32_t ui_due;

void boot()
{
    now_ms = 0;
    audio_due_ms = 0.0;
    ui_due = 0;
    held_btn = held_keys = 0;
    memset(enc_acc, 0, sizeof enc_acc);
    midi_w = midi_r = 0;
    tp::power_on();   // every engine and app variable as at power-on (the FM-1's RAM cleared)
    en_init();
    project_load();
    project_apply();
    lcd_fill(0, 0, 240, 240, 0);
    ui_init();
}

bool stepMs(int32_t* chunk)
{
    bool rendered = false;
    now_ms++;
    if (audio_due_ms <= (double)now_ms)   // (256 frames are 5.8 ms: at most one block falls due in a ms)
    {
        en_render(chunk, 256, master_q12);
        audio_due_ms += 256.0 * 1000.0 / 44100.0;
        rendered = true;
    }
    if (now_ms >= ui_due)
    {
        ui_due = now_ms + 16;
        ui_frame();
    }
    else
    {
        ui_input_only();
    }
    return rendered;
}

void setButtons(uint32_t mask) { held_btn = mask; }
void setKeys(uint32_t mask) { held_keys = mask; }
void turn(int encoder, int detents) { enc_acc[encoder] += detents; }
void setAccelOff(bool off) { accel_off = off ? 1 : 0; }
void setMaster(uint32_t q12) { master_q12 = q12; }
void midiIn(uint32_t pkt)
{
    if (midi_w - midi_r < TP_MQ) midi_q[midi_w++ % TP_MQ] = pkt;
}
uint32_t litButtons() { return lit_btn; }
uint32_t litKeys() { return lit_keys; }

// The project (the piece playing and every piece as it was left) as a flash object, and back.
uint32_t storeGen() { return store_gen; }
int storeRead(uint8_t* dst, int max) { return plat_store_load(OBJ_PROJ, dst, (uint32_t)max); }
void storeWrite(const uint8_t* src, int len)
{
    if (len < 0) { store_len[OBJ_PROJ] = -1; return; }
    plat_store_save(OBJ_PROJ, src, (uint32_t)len);
}

// A value as the app writes it when its knob turns: the project, the engine, and what follows from it.
// Every write from the MPC counts as a turn (ui.turn_t), as on the FM-1, so a pending shake or new-chords hold
// is cancelled by turning a knob while holding a button.
static_assert(kKnobLeds == K_LEDS && kKnobSlot == K_SLOT && kKnobChord == K_CQ && kKnobType == K_CX &&
                  kKnobLane == K_LANE && kKnobLaneChance == K_LCH && kKnobFilter == K_FILTER && kKnobWet == K_DUB &&
                  kKnobEnergy == K_ENERGY && kKnobMode == K_MODE && kKnobLoop == K_PLAY,
              "app_api.h's knob codes must be ui.c's");
static_assert(sizeof(project_t) <= (size_t)kStoreMax, "the project must fit the store");
static_assert(EN_NCQ == 7 && EN_NCX == 7, "kChordCodes counts 7 kinds and 7 types");
void knob(int code, int v)
{
    if (code != P_SEQ2OCT && knob_get(code) == v) return;   // (Sequence 2's Octave writes even when equal: 3.2)
    knob_set(code, v);
    ui.turn_t = plat_ms();
}
int knobValue(int code) { return knob_get(code); }
void voice(int p, int v)   // ALGO on a layer's page, set to a value rather than stepped (it wraps on the FM-1)
{
    if (proj.par[p] == v) return;   // (set_voice would send the same voice again)
    set_voice(p, v);
    ui.turn_t = plat_ms();
}
void walk(int w)           // ALGO on the Chords page: the walk's character, its chords walked again at once
{
    w = ((w % EN_NWALK) + EN_NWALK) % EN_NWALK;
    if (w == (proj.par[P_PMODE] & 3) % EN_NWALK) return;   // (only a change walks again)
    knob_set(P_PMODE, (proj.par[P_PMODE] & EN_PLOOP) | w);
    en_prog_rewalk();
    ui.turn_t = plat_ms();
}
int walkValue() { return (proj.par[P_PMODE] & 3) % EN_NWALK; }
void setPiece(int i)       // PRESET, set to a piece rather than stepped
{
    if (i != proj.piece) set_piece(i);
    ui.turn_t = plat_ms();
}
int par(int p) { return proj.par[p]; }
int piece() { return proj.piece; }
int modeAt(int pos) { return MODE_ORDER[pos & 7]; }
const char* laneName(int lane) { return LANE_NAME[lane % EN_NDRUM]; }

// PLAY pressed (the FM-1's PLAY: rest, or fresh entropy and play from the top).
bool playing() { return en_playing != 0; }
void play(bool on)
{
    if (on == (en_playing != 0)) return;
    if (on) { en_seed(plat_ms()); en_play(1); }
    else en_play(0);
}

// What the FM-1's screen shows, for the MPC's.
int view() { return ui.view; }
const char* viewName(int v) { return v >= 0 && v < NVIEWS ? VIEW_NAME[v] : ""; }
bool perform() { return ui.perf != 0; }
bool locked() { return ui.lock != 0; }
bool soloed() { return ui.solo != 0; }
bool held() { return en_frozen != 0; }
bool external() { return en_ext != 0; }
bool dirty() { return ui.dirty != 0; }
int slot() { return ui.slot; }
uint32_t effectsHeld() { return ui.fx | ui.bfx | (uint32_t)ui.mv << 16; }   // (the grid's, the black keys', the moves)
int leadDegree() { return en_lead_deg; }
int numViews() { return NVIEWS; }

// The chord as ui.c's chord_now() names it, as a number its text can be made from on any thread. Two
// namers lie behind chord_now: en_chord_name (a root and the other pitch classes) and en_chord_label (a
// written slot's root, kind and type). Both read only constant tables.
static int chordMaskCode(const int* pcs, int n)
{
    uint32_t iv = 0;
    for (int i = 1; i < n; i++)
        iv |= 1u << ((pcs[i] - pcs[0] + 12) % 12);   // (as en_chord_name reads them)
    return 1 + (pcs[0] % 12) * 4096 + (int)(iv & 0xFFFu);
}
static int chordLabelCode(int r, int q, int x)   // a root, kind and type, as en_chord_label names them
{
    return kChordLabelBase + (((unsigned)r < 12u ? r : 0) * EN_NCQ + ((unsigned)q < EN_NCQ ? q : 0)) * EN_NCX +
           ((unsigned)x < EN_NCX ? x : 0);
}
int chordCode()
{
    const int s = en_chord_slot;
    int pcs[EN_NPAD + 1];
    if (s >= 0 && s < EN_NPROG && proj.par[P_CQ0 + s])   // a written slot (slot_label: a kind set is never a walk slot)
        return chordLabelCode(proj.par[P_CR0 + s], proj.par[P_CQ0 + s], proj.par[P_CX0 + s]);
    pcs[0] = en_chord_root;
    for (int k = 0; k < EN_NPAD; k++)
        pcs[k + 1] = en_chord[k] % 12;
    return chordMaskCode(pcs, EN_NPAD + 1);
}
void chordText(int code, char* buf)
{
    buf[0] = 0;
    if (code >= kChordLabelBase && code < kChordCodes)
    {
        const int c = code - kChordLabelBase;
        en_chord_label(c / (EN_NCQ * EN_NCX), c / EN_NCX % EN_NCQ, c % EN_NCX, buf);
    }
    else if (code >= 1 && code < kChordLabelBase)
    {
        const int root = (code - 1) / 4096, iv = (code - 1) % 4096;
        int pcs[13], n = 1;
        pcs[0] = root;
        for (int b = 0; b < 12; b++)
            if (iv >> b & 1) pcs[n++] = (root + b) % 12;
        en_chord_name(pcs, n, buf);
    }
}
const char* chordNow(char* buf) { return chord_now(buf); }

// The MPC's controls for the FM-1's gestures: the code each gesture runs in ui.c, without what it says or draws.
static_assert(kNumLayers == NLAYER && kNumNudge == NNUDGE && kNumFx == EN_NFX, "app_api.h's counts must be ui.c's");
static void acted() { ui.act_t = plat_ms(); }
void setHold(bool on)
{
    if (on == (en_frozen != 0)) return;
    en_freeze(on ? 1 : 0);
    acted();
}
void setLock(bool on)
{
    if (on == (ui.lock != 0)) return;
    lock_toggle();
    acted();
}
bool muted(int l) { return l >= 0 && l < NLAYER && (ui.mutes & LAYER_MUTE[l]) == LAYER_MUTE[l]; }
void setMute(int l, bool on)
{
    if (l < 0 || l >= NLAYER || on == muted(l)) return;
    if (on) ui.mutes |= LAYER_MUTE[l];
    else ui.mutes &= ~LAYER_MUTE[l];
    perf_mutes();
    acted();
}
bool soloed(int l) { return l >= 0 && l < NLAYER && (ui.solo & LAYER_MUTE[l]) == LAYER_MUTE[l]; }
void setSolo(int l, bool on)
{
    if (l < 0 || l >= NLAYER || on == soloed(l)) return;
    if (on) ui.solo |= LAYER_MUTE[l];
    else ui.solo &= ~LAYER_MUTE[l];
    perf_mutes();
    acted();
}
void forget() { en_lead_forget(); acted(); }
void nextChord() { en_next(); acted(); }
void newChords() { en_prog_rewalk(); acted(); }
void shakeAll()
{
    static const uint8_t ALL[5] = {0, 1, 2, 3, 5};   // (HOME held on: every layer but the space)
    nudge_quiet = 1;
    for (int j = 0; j < 5; j++)
        nudge(ALL[j], NUDGE_SHAKE);
    nudge_quiet = 0;
    acted();
}
void resetPiece()
{
    project_piece_reset();
    project_apply();
    oct_home_take();
    mac_home();
    en_regrow();
    mark_dirty();
    acted();
}
void nudgeLayer(int l, bool shake)
{
    if (l < 0 || l >= NNUDGE) return;
    nudge(l, shake ? NUDGE_SHAKE : NUDGE_HAND);
    acted();
}
bool step(int st) { return st >= 0 && st < 16 && (lane_steps(ui.lane) >> st & 1u); }
bool accent(int st)
{
    const int q = lane_seq(ui.lane);
    const uint32_t a = proj.par[P_SMODE] ? (uint32_t)(uint16_t)proj.par[P_SACC0 + q] : (uint32_t)en_seq_acc[q];
    return st >= 0 && st < 16 && (a >> st & 1u);
}
void setStep(int st, bool on)
{
    if (st < 0 || st >= 16 || on == step(st)) return;
    pattern_key(STEP_KEY[st], 0);
    acted();
}
void setAccent(int st, bool on)
{
    if (st < 0 || st >= 16 || on == accent(st)) return;
    pattern_key(STEP_KEY[st], 1u << BLACK_KEY[0]);   // (a black key held with it)
    acted();
}
int chordRoot()
{
    const int s = ui.slot;
    if (slot_is_gap(s)) return -1;
    if (slot_is_walk(s))
    {
        int r, q, x;
        walk_written(s, &r, &q, &x);
        return ((r % 12) + 12) % 12;
    }
    return ((proj.par[P_CR0 + s] % 12) + 12) % 12;
}
void setChordRoot(int r)
{
    if (r == chordRoot()) return;
    if (r < 0) knob_set(K_CQ, EN_CQ_EMPTY);   // (Empty: a gap)
    else slot_key((uint32_t)((r % 12 + 12 + 7) % 12));   // (key 0 is F: slot_key's root is (5 + key) % 12)
    acted();
}
void padLead(int d, bool down)
{
    if (d < 0 || d >= 16) return;
    en_lead_key(d, down ? 1 : 0);
    acted();
}
void padFx(int e, bool down)
{
    if (e < 0 || e >= EN_NFX) return;
    ui.fx = down ? ui.fx | 1u << e : ui.fx & ~(1u << e);
    en_fx(ui.fx | ui.bfx);
    acted();
}
static const char* const MOVE_NAME[kNumMoves] = {"Rise", "Close", "Transpose", "Sweep", "Open", "Shut", "Drop", "Echo"};
void padMove(int m, bool down)
{
    if (m < 0 || m >= kNumMoves) return;
    if (m < 3)
    {
        const uint8_t b = (uint8_t)(1u << m);   // (EN_MV_RISE, _CLOSE, _TRANSPOSE)
        ui.mv = (uint8_t)(down ? ui.mv | b : ui.mv & ~b);
        en_move(ui.mv);
    }
    else
    {
        const uint32_t b = 1u << (m - 3);       // (EN_PF_SWEEP .. _ECHO)
        ui.moves = down ? ui.moves | b : ui.moves & ~b;
        en_perf(ui.moves);
    }
    acted();
}
static_assert(EN_MV_RISE == 1 && EN_MV_CLOSE == 2 && EN_MV_TRANSPOSE == 4 && EN_PF_SWEEP == 1 && EN_PF_OPEN == 2 &&
                  EN_PF_CLOSE == 4 && EN_PF_DROP == 8 && EN_PF_ECHO == 16,
              "padMove's bits");
uint32_t perfMoves() { return ui.moves; }
const char* fxName(int e) { return e >= 0 && e < EN_NFX ? EN_FX_NAME[e] : ""; }
const char* moveName(int m) { return m >= 0 && m < kNumMoves ? MOVE_NAME[m] : ""; }

// Readouts for the star-chart screen: what the engine and ui.c keep, read and never changed.
int section() { return en_section; }
int phraseBar() { return en_phrase_bar; }
int evolveChance()   // (ui.c nudge_tick: Evolve 100, 40% of the phrases; 70, 20%; 40 and under, none)
{
    const int ev = proj.par[P_EVOLVE];
    if (ev <= 40 || ui.lock || en_frozen || !en_playing) return 0;
    return (ev - 40) * 2 / 3;
}
float layerActivity(int l) { return l >= 0 && l < NLAYER ? en_layer_act[l] : 0.0f; }
bool layerHeard(int l) { return l >= 0 && l < NLAYER && perf_sounds(l); }
float filterDrift(int l) { return l >= 0 && l < NLAYER ? en_fo[l] : 0.0f; }
int densityShift() { return en_dd_shift; }
uint32_t seqHits(int q) { return q >= 0 && q < 2 ? en_seq_hits[q] : 0u; }
uint32_t seqReps(int q) { return q >= 0 && q < 2 ? en_seq_reps[q] : 0u; }
// ring_dots keeps what it last showed while held: called here too, it is called with the same inputs as the app's
// drawing, so both see the same ring (test_screen ring_matches_ui).
int ringDots(bool* lastBar)
{
    int last = 0;
    const int n = ring_dots(&last);
    if (lastBar) *lastBar = last != 0;
    return n;
}
int progPos() { return en_prog_pos; }
bool loopMode() { return (proj.par[P_PMODE] & EN_PLOOP) != 0; }
int progCount() { return prog_count(); }
int walkCharacter() { return proj.par[P_PMODE] & 3; }
SlotState slotState(int i)
{
    if (i < 0 || i >= EN_NPROG || slot_is_gap(i)) return kSlotGap;
    return slot_is_walk(i) ? kSlotWalk : kSlotWritten;
}
int slotChordCode(int i)
{
    switch (slotState(i))
    {
    case kSlotWritten: return chordLabelCode(proj.par[P_CR0 + i], proj.par[P_CQ0 + i], proj.par[P_CX0 + i]);
    case kSlotWalk:   // (slot_label's pitch classes, not walk_written's chord: on piece 9 they name differently)
    {
        int pcs[EN_NPAD + 1], iv[4], t[4];
        const int d = (proj.par[P_CR0 + i] - 1) % scale_steps();
        en_chord_tones(proj.pool, proj.par[P_ROOT], d, t);
        en_voicing(proj.pool, d, proj.par[P_CX0 + i] & 1, iv);
        pcs[0] = t[0];
        for (int k = 0; k < EN_NPAD; k++)
            pcs[k + 1] = (t[0] + iv[k]) % 12;
        return chordMaskCode(pcs, EN_NPAD + 1);
    }
    default: return 0;
    }
}
// The slot whose chord sounds next, -1 none; *skipped: the gaps the engine changes onto before it (each holds the
// chord a change longer). en_chord_change: Endless moves every slot down one and plays slot 0 (a gap there holds);
// Loop plays the slots in turn to prog_count(), a gap holding the chord before it.
static int nextSlot(int* skipped)
{
    *skipped = 0;
    if (!loopMode())
    {
        for (int i = 1; i < EN_NPROG; i++)
            if (!slot_is_gap(i)) return *skipped = i - 1, i;
        return -1;
    }
    const int n = prog_count(), p = en_prog_pos;   // (-1 before the first: from slot 0)
    for (int k = 1; k <= n; k++)
    {
        const int i = (p + k) % n;
        if (!slot_is_gap(i)) return *skipped = k - 1, i;
    }
    return -1;
}
int nextChordCode()
{
    int skipped;
    const int i = nextSlot(&skipped);
    return i < 0 ? 0 : slotChordCode(i);
}
int barsToChange()   // (the bars gone as ring_dots counts them, so the count and its last bar agree)
{
    const int db = en_drift_bars(proj.par[P_DRIFT]);
    if (!db) return -1;
    const int k = (int)(en_drift * (float)db + 0.5f);
    if (k < db) return db - k;
    return en_frozen ? 0 : 1;   // (held: the engine counts on, en_drift stops at 1; let go, it changes at the next bar)
}
int barsToNextChord()
{
    if (en_frozen) return -2;
    const int db = en_drift_bars(proj.par[P_DRIFT]);
    int skipped;
    if (!db || nextSlot(&skipped) < 0) return -1;
    return barsToChange() + db * skipped;
}
int loopStep(int q) { return q >= 0 && q < 2 ? en_loop_pos[q] : 0; }
const char* slotLabel(int i, char* buf)
{
    buf[0] = 0;
    return i >= 0 && i < EN_NPROG ? slot_label(i, buf) : buf;
}

// The project as a save would write it: the piece playing kept into its slot (with its patterns), then the
// whole struct. keep() changes nothing the music reads until the next piece change, which keeps again.
int snapshot(uint8_t* dst, int max)
{
    if (max < (int)sizeof proj) return -1;
    keep();
    memcpy(dst, &proj, sizeof proj);
    return (int)sizeof proj;
}

} // namespace app
} // namespace tp
