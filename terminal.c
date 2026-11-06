#include "terminal.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  ((volatile uint16_t *)0xB8000)

static int row = 0;
static int col = 0;
static uint8_t color = (VGA_LIGHT_GREY) | (VGA_BLACK << 4);

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t vga_entry(char c, uint8_t attr) {
    return (uint16_t)(uint8_t)c | ((uint16_t)attr << 8);
}

static void update_cursor(void) {
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void) {
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', color);
    }
    row = VGA_HEIGHT - 1;
}

void set_color(enum vga_color fg, enum vga_color bg) {
    color = (uint8_t)(fg | (bg << 4));
}

void clear_screen(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i] = vga_entry(' ', color);
    }
    row = 0;
    col = 0;
    update_cursor();
}

void print_char(char c) {
    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\r') {
        col = 0;
    } else if (c == '\t') {
        col = (col + 4) & ~3;
    } else if (c == '\b') {
        if (col > 0) {
            col--;
            VGA_MEMORY[row * VGA_WIDTH + col] = vga_entry(' ', color);
        }
    } else {
        VGA_MEMORY[row * VGA_WIDTH + col] = vga_entry(c, color);
        col++;
    }

    if (col >= VGA_WIDTH) {
        col = 0;
        row++;
    }
    if (row >= VGA_HEIGHT) {
        scroll();
    }
    update_cursor();
}

void print_string(const char *s) {
    while (*s) {
        print_char(*s++);
    }
}

void print_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    print_string("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        print_char(digits[(value >> shift) & 0xF]);
    }
}
