#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

#define MB_INFO_MMAP (1u << 6)

struct mb_mmap_entry {
    uint32_t size;          /* size of the rest of the entry */
    uint64_t addr;
    uint64_t len;
    uint32_t type;          /* 1 = usable RAM */
} __attribute__((packed));

struct mb_info {
    uint32_t flags;
    uint32_t mem_lower, mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count, mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length, mmap_addr;
} __attribute__((packed));

#endif
