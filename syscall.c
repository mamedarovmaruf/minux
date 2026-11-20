#include "syscall.h"
#include "sched.h"
#include "proc.h"
#include "paging.h"
#include "pmm.h"
#include "kheap.h"
#include "kstring.h"
#include "terminal.h"
#include "keyboard.h"
#include "video.h"
#include "fat.h"
#include "pit.h"
#include "io.h"
#include "panic.h"
#include "userbin.h"

extern char kernel_end;

/* ---- user pointer checks: ring 3 must never make the kernel touch
   memory outside its own mapped pages ---- */

static int user_range_ok(uint32_t addr, uint32_t len) {
    if (addr < USER_BASE || len > USER_STACK_TOP - USER_BASE) return 0;
    if (addr > USER_STACK_TOP - len) return 0;
    addrspace_t *as = sched_space();
    for (uint32_t p = addr & ~(PAGE_SIZE - 1); p < addr + len; p += PAGE_SIZE) {
        if (!paging_phys(as, p)) return 0;
    }
    return 1;
}

/* Returns the string length, or -1 if it is not terminated within max. */
static int user_str_len(uint32_t addr, uint32_t max) {
    for (uint32_t i = 0; i < max; i++) {
        if ((i == 0 || ((addr + i) & (PAGE_SIZE - 1)) == 0) && !user_range_ok(addr + i, 1)) {
            return -1;
        }
        if (((const char *)(uintptr_t)addr)[i] == 0) return (int)i;
    }
    return -1;
}

#define NAME_MAX_LEN 64

static void machine_reboot(void) {
    __asm__ volatile ("cli");
    for (int i = 0; i < 1000; i++) {
        if (!(inb(0x64) & 2)) break;
    }
    outb(0x64, 0xFE);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

uint32_t syscall_handler(struct regs *r) {
    uint32_t a = r->ebx, b = r->ecx, c = r->edx, d = r->esi;
    int ret = 0;

    switch (r->eax) {
    case SYS_EXIT:
        return sched_exit(r);

    /* terminal */
    case SYS_PUTC:
        print_char((char)a);
        break;
    case SYS_WRITE:
        if (!user_range_ok(a, b)) { ret = -1; break; }
        for (uint32_t i = 0; i < b; i++) print_char(((const char *)(uintptr_t)a)[i]);
        break;
    case SYS_SET_COLOR:
        set_color((enum vga_color)(a & 15), (enum vga_color)(b & 15));
        break;
    case SYS_CLEAR:
        clear_screen();
        break;
    case SYS_PUT_CHAR_AT:
        put_char_at(a, b, (char)c, (uint8_t)d);
        break;
    case SYS_SET_CURSOR:
        set_cursor(a, b);
        break;
    case SYS_CLEAR_LINE:
        clear_line(a);
        break;
    case SYS_TERM_SIZE:
        ret = (int)((term_cols() << 16) | (term_rows() & 0xFFFF));
        break;
    case SYS_VIDEO_INFO:
        terminal_print_video_info();
        break;

    /* keyboard: no data yet -> block and run the syscall again later */
    case SYS_GETCHAR: {
        int ch = keyboard_read_nowait();
        if (ch < 0) return sched_wait_key(r);
        ret = ch;
        break;
    }
    case SYS_HAS_CHAR:
        ret = keyboard_has_char();
        break;

    /* time and tasks */
    case SYS_SLEEP:
        if (a > 3600000u) a = 3600000u;
        r->eax = 0;
        return sched_sleep(r, (a * pit_hz() + 999) / 1000);
    case SYS_YIELD:
        r->eax = 0;
        return sched_yield(r);
    case SYS_UPTIME:
        ret = (int)pit_uptime_ms();
        break;
    case SYS_SPAWN:
        ret = user_str_len(a, TASK_NAME_MAX) < 0 ? -1 : proc_spawn((const char *)(uintptr_t)a);
        break;
    case SYS_KILL:
        ret = sched_kill((int)a);
        break;
    case SYS_PS:
        if (b == 0 || b > MAX_TASKS || !user_range_ok(a, b * sizeof(struct psinfo))) { ret = -1; break; }
        ret = sched_ps((struct psinfo *)(uintptr_t)a, (int)b);
        break;
    case SYS_MEMINFO: {
        if (!user_range_ok(a, sizeof(struct meminfo))) { ret = -1; break; }
        struct meminfo *m = (struct meminfo *)(uintptr_t)a;
        m->total_pages = pmm_total_pages();
        m->free_pages  = pmm_free_pages();
        kheap_stats(&m->heap_used, &m->heap_free, &m->heap_blocks);
        m->fb_addr     = video_get()->fb_addr;
        m->kernel_end  = (uint32_t)(uintptr_t)&kernel_end;
        break;
    }

    /* filesystem */
    case SYS_FAT_READDIR:
        if (!user_range_ok(a, sizeof(uint32_t)) || !user_range_ok(b, sizeof(struct fat_entry))) { ret = -1; break; }
        ret = fat_readdir((uint32_t *)(uintptr_t)a, (struct fat_entry *)(uintptr_t)b);
        break;
    case SYS_FAT_CREATE:
        if (user_str_len(a, NAME_MAX_LEN) < 0) { ret = FAT_ERR_NAME; break; }
        ret = fat_create((const char *)(uintptr_t)a);
        break;
    case SYS_FAT_REMOVE:
        if (user_str_len(a, NAME_MAX_LEN) < 0) { ret = FAT_ERR_NAME; break; }
        ret = fat_remove((const char *)(uintptr_t)a);
        break;
    case SYS_FAT_READ:      /* name, offset, buf, len */
        if (user_str_len(a, NAME_MAX_LEN) < 0 || !user_range_ok(c, d)) { ret = FAT_ERR_NAME; break; }
        ret = fat_read((const char *)(uintptr_t)a, b, (void *)(uintptr_t)c, d);
        break;
    case SYS_FAT_WRITE:     /* name, data, len */
        if (user_str_len(a, NAME_MAX_LEN) < 0 || !user_range_ok(b, c)) { ret = FAT_ERR_NAME; break; }
        ret = fat_write((const char *)(uintptr_t)a, (const void *)(uintptr_t)b, c);
        break;
    case SYS_FAT_APPEND:
        if (user_str_len(a, NAME_MAX_LEN) < 0 || !user_range_ok(b, c)) { ret = FAT_ERR_NAME; break; }
        ret = fat_append((const char *)(uintptr_t)a, (const void *)(uintptr_t)b, c);
        break;
    case SYS_FAT_INFO:
        if (!user_range_ok(a, sizeof(struct fat_info))) { ret = -1; break; }
        ret = fat_get_info((struct fat_info *)(uintptr_t)a);
        break;
    case SYS_FAT_FORMAT:
        ret = fat_format();
        break;

    /* machine */
    case SYS_REBOOT:
        machine_reboot();
        break;
    case SYS_HALT:
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
        break;

    default:
        ret = -1;
        break;
    }

    r->eax = (uint32_t)ret;
    return (uint32_t)(uintptr_t)r;
}
