#include "proc.h"
#include "kstring.h"

/* Symbols come from `ld -r -b binary` on build/<name>.bin.
   To add a program: create user/<name>/, then list it here. */
extern const uint8_t _binary_mos_bin_start[], _binary_mos_bin_end[];
extern const uint8_t _binary_clock_bin_start[], _binary_clock_bin_end[];

static const struct prog progs[] = {
    { "mos",   _binary_mos_bin_start,   _binary_mos_bin_end   },
    { "clock", _binary_clock_bin_start, _binary_clock_bin_end },
};

const struct prog *prog_find(const char *name) {
    for (uint32_t i = 0; i < sizeof(progs) / sizeof(progs[0]); i++) {
        if (kstrcmp(name, progs[i].name) == 0) {
            return &progs[i];
        }
    }
    return 0;
}
