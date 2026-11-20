// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: the FM-1's power switch (see power.h).
#include "power.h"

#include <cstdlib>
#include <cstring>

#if defined(__APPLE__)
extern char tp_data_start __asm("section$start$__DATA$__tp_data");
extern char tp_data_end __asm("section$end$__DATA$__tp_data");
extern char tp_bss_start __asm("section$start$__DATA$__tp_bss");
extern char tp_bss_end __asm("section$end$__DATA$__tp_bss");
#define TP_DATA_BEGIN (&tp_data_start)
#define TP_DATA_END (&tp_data_end)
#define TP_BSS_BEGIN (&tp_bss_start)
#define TP_BSS_END (&tp_bss_end)
#else
extern "C" char __start_tp_data[], __stop_tp_data[], __start_tp_bss[], __stop_tp_bss[];
#define TP_DATA_BEGIN (__start_tp_data)
#define TP_DATA_END (__stop_tp_data)
#define TP_BSS_BEGIN (__start_tp_bss)
#define TP_BSS_END (__stop_tp_bss)
#endif

namespace tp {
namespace {

// The sections are copied whole. Under AddressSanitizer the gaps between variables are poisoned red zones,
// so the copies there go byte by byte, uninstrumented (a plain memcpy would be reported).
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TP_ASAN 1
#endif
#endif
#if defined(TP_ASAN)
__attribute__((no_sanitize("address"))) void copyBytes(char* dst, const char* src, long n)
{
    volatile char* d = dst;
    for (long i = 0; i < n; ++i) d[i] = src[i];
}
__attribute__((no_sanitize("address"))) void zeroBytes(char* dst, long n)
{
    volatile char* d = dst;
    for (long i = 0; i < n; ++i) d[i] = 0;
}
#else
void copyBytes(char* dst, const char* src, long n) { std::memcpy(dst, src, (size_t)n); }
void zeroBytes(char* dst, long n) { std::memset(dst, 0, (size_t)n); }
#endif

// The initialised variables as the library was loaded, before any of NACHTFAHRT's code ran (the plugin's
// constructor only calls the engine's pure functions: the parameter table, the piece names).
struct PowerOnImage
{
    char* data = nullptr;
    long size = 0;
    PowerOnImage()
    {
        size = (long)(TP_DATA_END - TP_DATA_BEGIN);
        data = size > 0 ? static_cast<char*>(std::malloc((size_t)size)) : nullptr;
        if (data) copyBytes(data, TP_DATA_BEGIN, size);
    }
};

PowerOnImage& image()
{
    static PowerOnImage img;
    return img;
}
const PowerOnImage& kTakenAtLoad = image();   // (taken during static initialisation)

} // namespace

void power_on()
{
    const PowerOnImage& img = image();
    if (img.data) copyBytes(TP_DATA_BEGIN, img.data, img.size);
    zeroBytes(TP_BSS_BEGIN, (long)(TP_BSS_END - TP_BSS_BEGIN));
}

long power_data_bytes() { return (long)(TP_DATA_END - TP_DATA_BEGIN); }
long power_bss_bytes() { return (long)(TP_BSS_END - TP_BSS_BEGIN); }

} // namespace tp
