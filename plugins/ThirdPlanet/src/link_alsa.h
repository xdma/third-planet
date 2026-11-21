// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: the ALSA sequencer as Harmony Link uses it (docs/harmony-link-device-facts.md: the MPC's MIDI
// plumbing is ALSA sequencer, and a plugin's own client reaches the USB MIDI function "f_midi" directly).
// libasound is dlopen()ed, never linked: the SDK's toolchains have no ALSA headers, and a system without the
// library (the Mac, the host simulator) simply has no link. So the few types and numbers of the ABI are written
// out here, from alsa/seq_event.h and alsa/seq.h. Called from the link worker's thread only, never the audio thread.
// (Defined in link_alsa.cpp only with TP_LINK=1.)
#pragma once

#include <cstddef>
#include <cstdint>

namespace tp::link {

// snd_seq_event_t, 28 bytes on 32-bit and 64-bit ARM alike: the variable-length payload is ALSA's packed
// snd_seq_ev_ext_t {len, ptr}, 8 or 12 bytes, inside the 12-byte data union.
struct SeqAddr { unsigned char client, port; };
struct SeqExt
{
    unsigned int len;
    void* ptr;
} __attribute__((packed));
struct SeqEvent
{
    unsigned char type, flags, tag, queue;
    unsigned int time[2];
    SeqAddr source, dest;
    union
    {
        SeqExt ext;
        unsigned char raw8[12];
        unsigned int raw32[3];
    } data;
};
namespace abi {   // (the layout, for either pointer size, whatever this build's)
template <class Ptr>
struct Ext
{
    unsigned int len;
    Ptr ptr;
} __attribute__((packed));
static_assert(sizeof(Ext<uint32_t>) == 8 && sizeof(Ext<uint64_t>) == 12, "snd_seq_ev_ext_t: 32-bit and 64-bit");
static_assert(offsetof(Ext<uint32_t>, ptr) == 4 && offsetof(Ext<uint64_t>, ptr) == 4, "snd_seq_ev_ext_t: ptr follows len");
} // namespace abi
static_assert(sizeof(SeqExt) == 4 + sizeof(void*) && sizeof(SeqExt) <= 12, "snd_seq_ev_ext_t layout");
static_assert(sizeof(SeqEvent) == 28, "snd_seq_event_t layout");
static_assert(offsetof(SeqEvent, source) == 12 && offsetof(SeqEvent, data) == 16, "snd_seq_event_t layout");

enum : int
{
    kSeqOpenDuplex = 3,
    kSeqCapRead = 1 << 0, kSeqCapWrite = 1 << 1, kSeqCapSubsRead = 1 << 5, kSeqCapSubsWrite = 1 << 6,
    kSeqTypeMidiGeneric = 1 << 1, kSeqTypeApplication = 1 << 20,
    kSeqEvSysex = 130,             // SND_SEQ_EVENT_SYSEX
    kSeqLengthVariable = 1 << 2,   // SND_SEQ_EVENT_LENGTH_VARIABLE (flags), of the mask ...
    kSeqLengthMask = 3 << 2,       // ... SND_SEQ_EVENT_LENGTH_MASK
    kSeqQueueDirect = 253, kSeqAddrSubscribers = 254, kSeqAddrUnknown = 253,
    kSeqEAgain = 11,               // Linux's EAGAIN (the library is Linux's): -11 is "not now", not an error
};

// The libasound subset (snd_seq_*), as plain function pointers so that a test can stand in for it. ev is a SeqEvent.
struct SeqApi
{
    bool ok = false;   // false: no libasound here; nothing below may be called
    int (*open)(void**, const char*, int, int) = nullptr;
    int (*close)(void*) = nullptr;
    int (*setName)(void*, const char*) = nullptr;
    int (*createPort)(void*, const char*, unsigned, unsigned) = nullptr;
    int (*outputDirect)(void*, void* ev) = nullptr;
    int (*connectTo)(void*, int, int, int) = nullptr;
    int (*connectFrom)(void*, int, int, int) = nullptr;
    int (*nonblock)(void*, int) = nullptr;
    int (*input)(void*, void** ev) = nullptr;
    int (*findClient)(const char* name) = nullptr;   // the client of that name in /proc/asound/seq/clients; -1: none
};

// dlopen("libasound.so.2"), once per process (the library stays loaded). ok = false if it, or a function, is
// missing. Allocates and reads files: not for the audio thread.
SeqApi loadAlsa();

} // namespace tp::link
