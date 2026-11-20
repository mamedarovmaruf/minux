#ifndef USERBIN_H
#define USERBIN_H

#include <stdint.h>

/* User address space layout. Keep USER_BASE in sync with user/user.ld.
   Above typical RAM (identity mapped) and below the kernel heap. */
#define USER_BASE        0xB0000000u
#define USER_STACK_TOP   0xBFFFF000u
#define USER_STACK_SIZE  (16u * 1024u)
#define USER_MAX_IMAGE   (4u * 1024u * 1024u)
#define USER_MAGIC       0x5855454Du            /* "MEUX" */

/* First bytes of every embedded user program (written by user/user.ld). */
struct user_header {
    uint32_t magic;
    uint32_t entry;
    uint32_t file_size;     /* bytes stored in the image */
    uint32_t mem_size;      /* bytes in memory, file_size + bss */
};

#endif
