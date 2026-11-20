#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* int 0x80 ABI: eax = number, ebx/ecx/edx/esi = args, result in eax. */
enum {
    SYS_EXIT = 1,
    SYS_PUTC,
    SYS_WRITE,
    SYS_SET_COLOR,
    SYS_CLEAR,
    SYS_PUT_CHAR_AT,
    SYS_SET_CURSOR,
    SYS_CLEAR_LINE,
    SYS_TERM_SIZE,
    SYS_VIDEO_INFO,
    SYS_GETCHAR,
    SYS_HAS_CHAR,
    SYS_SLEEP,
    SYS_YIELD,
    SYS_UPTIME,
    SYS_SPAWN,
    SYS_KILL,
    SYS_PS,
    SYS_MEMINFO,
    SYS_FAT_READDIR,
    SYS_FAT_CREATE,
    SYS_FAT_READ,
    SYS_FAT_WRITE,
    SYS_FAT_APPEND,
    SYS_FAT_REMOVE,
    SYS_FAT_INFO,
    SYS_FAT_FORMAT,
    SYS_REBOOT,
    SYS_HALT
};

enum task_state {
    TASK_FREE, TASK_READY, TASK_SLEEP, TASK_WAIT_KEY, TASK_ZOMBIE,
    TASK_RUNNING                        /* only reported by SYS_PS */
};

#define TASK_NAME_MAX 16

struct psinfo {
    uint32_t pid;
    uint32_t state;
    char     name[TASK_NAME_MAX];
};

struct regs;
uint32_t syscall_handler(struct regs *r);       /* kernel side, called from isr_handler */

struct meminfo {
    uint32_t total_pages, free_pages;
    uint32_t heap_used, heap_free, heap_blocks;
    uint32_t fb_addr;
    uint32_t kernel_end;
};

#endif
