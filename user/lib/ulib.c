#include "ulib.h"
#include "terminal.h"
#include "keyboard.h"
#include "fat.h"
#include "kstring.h"

static inline int sc(int n, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    int ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d)
                      : "memory");
    return ret;
}

#define P(x) ((uint32_t)(uintptr_t)(x))

/* process */

void sys_exit(int code) {
    sc(SYS_EXIT, (uint32_t)code, 0, 0, 0);
    for (;;) { }
}

void sys_sleep(uint32_t ms)       { sc(SYS_SLEEP, ms, 0, 0, 0); }
void sys_yield(void)              { sc(SYS_YIELD, 0, 0, 0, 0); }
uint32_t sys_uptime_ms(void)      { return (uint32_t)sc(SYS_UPTIME, 0, 0, 0, 0); }
int sys_spawn(const char *name)   { return sc(SYS_SPAWN, P(name), 0, 0, 0); }
int sys_kill(int pid)             { return sc(SYS_KILL, (uint32_t)pid, 0, 0, 0); }
int sys_ps(struct psinfo *out, int max) { return sc(SYS_PS, P(out), (uint32_t)max, 0, 0); }
void sys_meminfo(struct meminfo *out)   { sc(SYS_MEMINFO, P(out), 0, 0, 0); }

void sys_reboot(void) {
    sc(SYS_REBOOT, 0, 0, 0, 0);
    for (;;) { }
}

void sys_halt(void) {
    sc(SYS_HALT, 0, 0, 0, 0);
    for (;;) { }
}

/* terminal.h */

void print_char(char c)           { sc(SYS_PUTC, (uint32_t)(uint8_t)c, 0, 0, 0); }
void print_string(const char *s)  { sc(SYS_WRITE, P(s), kstrlen(s), 0, 0); }
void clear_screen(void)           { sc(SYS_CLEAR, 0, 0, 0, 0); }
void set_color(enum vga_color fg, enum vga_color bg) { sc(SYS_SET_COLOR, (uint32_t)fg, (uint32_t)bg, 0, 0); }
void put_char_at(uint32_t row, uint32_t col, char c, uint8_t color) {
    sc(SYS_PUT_CHAR_AT, row, col, (uint32_t)(uint8_t)c, color);
}
void set_cursor(uint32_t row, uint32_t col) { sc(SYS_SET_CURSOR, row, col, 0, 0); }
void clear_line(uint32_t row)     { sc(SYS_CLEAR_LINE, row, 0, 0, 0); }
void terminal_print_video_info(void) { sc(SYS_VIDEO_INFO, 0, 0, 0, 0); }

/* The screen size never changes at run time, ask once. */
static uint32_t size_cache;

static void load_size(void) {
    if (!size_cache) size_cache = (uint32_t)sc(SYS_TERM_SIZE, 0, 0, 0, 0);
}

uint32_t term_cols(void) { load_size(); return size_cache >> 16; }
uint32_t term_rows(void) { load_size(); return size_cache & 0xFFFF; }

void print_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char buf[10];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = digits[(value >> (28 - 4 * i)) & 0xF];
    }
    sc(SYS_WRITE, P(buf), 10, 0, 0);
}

void print_dec(uint32_t value) {
    char buf[11];
    int i = 0;
    if (value == 0) {
        print_char('0');
        return;
    }
    while (value > 0) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    char out[11];
    for (int j = 0; j < i; j++) {
        out[j] = buf[i - 1 - j];
    }
    sc(SYS_WRITE, P(out), (uint32_t)i, 0, 0);
}

/* keyboard.h */

char keyboard_getchar(void)  { return (char)sc(SYS_GETCHAR, 0, 0, 0, 0); }
int  keyboard_has_char(void) { return sc(SYS_HAS_CHAR, 0, 0, 0, 0); }

/* fat.h */

int fat_readdir(uint32_t *cursor, struct fat_entry *out) { return sc(SYS_FAT_READDIR, P(cursor), P(out), 0, 0); }
int fat_create(const char *name) { return sc(SYS_FAT_CREATE, P(name), 0, 0, 0); }
int fat_remove(const char *name) { return sc(SYS_FAT_REMOVE, P(name), 0, 0, 0); }
int fat_read(const char *name, uint32_t offset, void *buf, uint32_t len) {
    return sc(SYS_FAT_READ, P(name), offset, P(buf), len);
}
int fat_write(const char *name, const void *data, uint32_t len) {
    return sc(SYS_FAT_WRITE, P(name), P(data), len, 0);
}
int fat_append(const char *name, const void *data, uint32_t len) {
    return sc(SYS_FAT_APPEND, P(name), P(data), len, 0);
}
int fat_get_info(struct fat_info *out) { return sc(SYS_FAT_INFO, P(out), 0, 0, 0); }
int fat_format(void) { return sc(SYS_FAT_FORMAT, 0, 0, 0, 0); }

const char *fat_strerror(int err) {
    switch (err) {
        case FAT_ERR_NODISK:   return "no disk found";
        case FAT_ERR_NOFS:     return "no FAT16 filesystem, run 'format yes'";
        case FAT_ERR_IO:       return "disk I/O error";
        case FAT_ERR_NOTFOUND: return "no such file";
        case FAT_ERR_EXISTS:   return "file already exists";
        case FAT_ERR_NAME:     return "invalid name (8.3 format: up to 8 + 3 characters)";
        case FAT_ERR_FULL:     return "disk or directory is full";
        case FAT_ERR_ISDIR:    return "is a directory";
        case FAT_ERR_CORRUPT:  return "filesystem is corrupted";
        case FAT_ERR_TOOBIG:   return "file too large";
        case FAT_ERR_SMALL:    return "disk too small for FAT16";
        default:               return "unknown error";
    }
}
