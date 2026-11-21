// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: libasound, dlopen()ed (link_alsa.h).
#include "tp_features.h"

#if TP_LINK

#include "link_alsa.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>

namespace tp::link {

namespace {

// "Client  24 : "f_midi" [Kernel Legacy]" in /proc/asound/seq/clients.
int findClient(const char* name)
{
    FILE* f = std::fopen("/proc/asound/seq/clients", "r");
    if (!f) return -1;
    char line[256], got[128];
    int found = -1, id = 0;
    while (std::fgets(line, sizeof line, f))
        if (std::sscanf(line, "Client %d : \"%127[^\"]\"", &id, got) == 2 && std::strcmp(got, name) == 0)
        {
            found = id;
            break;
        }
    std::fclose(f);
    return found;
}

template <class Fn>
bool sym(void* lib, Fn& fn, const char* name)
{
    fn = reinterpret_cast<Fn>(dlsym(lib, name));
    return fn != nullptr;
}

SeqApi load()
{
    SeqApi a;
    void* lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return a;
    bool ok = true;
    ok &= sym(lib, a.open, "snd_seq_open");
    ok &= sym(lib, a.close, "snd_seq_close");
    ok &= sym(lib, a.setName, "snd_seq_set_client_name");
    ok &= sym(lib, a.createPort, "snd_seq_create_simple_port");
    ok &= sym(lib, a.outputDirect, "snd_seq_event_output_direct");
    ok &= sym(lib, a.connectTo, "snd_seq_connect_to");
    ok &= sym(lib, a.connectFrom, "snd_seq_connect_from");
    ok &= sym(lib, a.nonblock, "snd_seq_nonblock");
    ok &= sym(lib, a.input, "snd_seq_event_input");
    a.findClient = findClient;
    if (!ok)
    {
        dlclose(lib);
        return SeqApi{};
    }
    a.ok = true;
    return a;
}

} // namespace

SeqApi loadAlsa()
{
    static const SeqApi api = load();   // (once, thread-safe; a failure is not tried again)
    return api;
}

} // namespace tp::link

#endif // TP_LINK
