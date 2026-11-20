#ifndef PROC_H
#define PROC_H

#include <stdint.h>

/* A user program linked into the kernel image by the build tool. */
struct prog {
    const char    *name;
    const uint8_t *start, *end;
};

const struct prog *prog_find(const char *name);
int proc_spawn(const char *name);       /* pid, or -1 */

#endif
