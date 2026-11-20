// Prints the engine's parameter table (index, name, lo, hi, default) and each piece's recipe.
//   c++ -std=c++17 -O1 -I plugins/ThirdPlanet/src plugins/ThirdPlanet/tests/dump_params.cpp \
//       plugins/ThirdPlanet/src/engine_unit.cpp -o plugins/ThirdPlanet/build/tests/dump_params
#include <cstdio>
extern "C" {
#include "nachtfahrt/dsp/amb.h"
}
int main()
{
    std::printf("P_NPARAMS %d\n", (int)P_NPARAMS);
    for (int i = 0; i < P_NPARAMS; i++) {
        const en_param_t* p = en_param_info(i);
        char t[16];
        en_param_text(i, p->def, t);
        std::printf("%3d %-14s %6d %6d %6d  %s\n", i, p->name, p->lo, p->hi, p->def, t);
    }
    for (int k = 0; k < EN_NPIECES; k++) {
        int16_t par[P_NPARAMS];
        uint16_t pool;
        en_piece(k, par, &pool);
        std::printf("piece %d %s pool %03x\n", k, en_piece_name(k), pool);
    }
}
