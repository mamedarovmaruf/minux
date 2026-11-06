#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>

enum vga_color {
    VGA_BLACK = 0,  VGA_BLUE = 1,        VGA_GREEN = 2,       VGA_CYAN = 3,
    VGA_RED = 4,    VGA_MAGENTA = 5,     VGA_BROWN = 6,       VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8, VGA_LIGHT_BLUE = 9, VGA_LIGHT_GREEN = 10, VGA_LIGHT_CYAN = 11,
    VGA_LIGHT_RED = 12, VGA_LIGHT_MAGENTA = 13, VGA_YELLOW = 14, VGA_WHITE = 15
};

void clear_screen(void);
void set_color(enum vga_color fg, enum vga_color bg);
void print_char(char c);
void print_string(const char *s);
void print_hex(uint32_t value);
void print_dec(uint32_t value);

#endif
