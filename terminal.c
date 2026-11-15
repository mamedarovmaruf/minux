#include "terminal.h"
#include "video.h"
#include "io.h"

#define TEXT_COLS   80
#define TEXT_ROWS   25
#define VGA_MEMORY  ((volatile uint16_t *)0xB8000)

#define MAX_COLS    512
#define MAX_ROWS    192

#ifndef TERM_FONT_SCALE_X2
#define TERM_FONT_SCALE_X2 0  /* 0 = auto (1x at 1080p), 2 = 1x, 3 = 1.5x, 4 = 2x */
#endif
static int row = 0;
static int col = 0;
static uint8_t color = (VGA_LIGHT_GREY) | (VGA_BLACK << 4);

static uint32_t cols = TEXT_COLS;
static uint32_t rows = TEXT_ROWS;

static int gfx = 0;
static uint32_t scale2 = 2;                 
static uint32_t cell_w = 8, cell_h = 16;
static uint32_t off_x = 0, off_y = 0;
static uint16_t cells[MAX_COLS * MAX_ROWS];
static int cur_x = -1, cur_y = -1;       

static inline uint16_t vga_entry(char c, uint8_t attr) {
    return (uint16_t)(uint8_t)c | ((uint16_t)attr << 8);
}

static void draw_cell(uint32_t x, uint32_t y, uint16_t entry, int underline) {
    uint8_t attr = (uint8_t)(entry >> 8);
    video_glyph(off_x + x * cell_w, off_y + y * cell_h, (uint8_t)(entry & 0xFF),
                video_color(attr & 0x0F), video_color(attr >> 4), scale2, underline);
}

static void hide_cursor(void) {
    if (gfx && cur_x >= 0) {
        draw_cell((uint32_t)cur_x, (uint32_t)cur_y, cells[cur_y * (int)cols + cur_x], 0);
        cur_x = -1;
    }
}

static void cell_put(uint32_t x, uint32_t y, char c, uint8_t attr) {
    uint16_t entry = vga_entry(c, attr);
    if (gfx) {
        cells[y * cols + x] = entry;
        draw_cell(x, y, entry, 0);
    } else {
        VGA_MEMORY[y * TEXT_COLS + x] = entry;
    }
}

static void update_cursor(void) {
    if (gfx) {
        hide_cursor();
        cur_x = col;
        cur_y = row;
        draw_cell((uint32_t)col, (uint32_t)row, cells[row * (int)cols + col], 1);
    } else {
        uint16_t pos = (uint16_t)(row * TEXT_COLS + col);
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    }
}

static void scroll(void) {
    if (gfx) {
        hide_cursor();
        video_copy(cells, cells + cols, (uint32_t)((rows - 1) * cols * sizeof(cells[0])));
        for (uint32_t x = 0; x < cols; x++) {
            cells[(rows - 1) * cols + x] = vga_entry(' ', color);
        }
        video_scroll(off_y, rows * cell_h, cell_h);
        video_fill(0, off_y + (rows - 1) * cell_h, video_get()->width, cell_h,
                   video_color(color >> 4));
    } else {
        for (uint32_t y = 1; y < TEXT_ROWS; y++) {
            for (uint32_t x = 0; x < TEXT_COLS; x++) {
                VGA_MEMORY[(y - 1) * TEXT_COLS + x] = VGA_MEMORY[y * TEXT_COLS + x];
            }
        }
        for (uint32_t x = 0; x < TEXT_COLS; x++) {
            VGA_MEMORY[(TEXT_ROWS - 1) * TEXT_COLS + x] = vga_entry(' ', color);
        }
    }
    row = (int)rows - 1;
}

void terminal_init(void) {
    const struct video_info *v = video_get();
    if (!v->graphics) {
        gfx = 0;
        cols = TEXT_COLS;
        rows = TEXT_ROWS;
        return;
    }

    scale2 = TERM_FONT_SCALE_X2;
    if (scale2 == 0) {
        
        uint32_t sx = v->width * 2 / 1920, sy = v->height * 2 / 1080;
        scale2 = sx < sy ? sx : sy;
        scale2 &= ~1u;                     
    }
    if (scale2 < 2) scale2 = 2;
    if (scale2 > 2 * VIDEO_MAX_SCALE) scale2 = 2 * VIDEO_MAX_SCALE;

    cell_w = 4 * scale2;
    cell_h = 8 * scale2;
    cols = v->width / cell_w;
    rows = v->height / cell_h;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    off_x = (v->width  - cols * cell_w) / 2;
    off_y = (v->height - rows * cell_h) / 2;

    gfx = 1;
    cur_x = cur_y = -1;
    clear_screen();
}

uint32_t term_cols(void) { return cols; }
uint32_t term_rows(void) { return rows; }

void set_color(enum vga_color fg, enum vga_color bg) {
    color = (uint8_t)(fg | (bg << 4));
}

void put_char_at(uint32_t y, uint32_t x, char c, uint8_t attr) {
    if (y >= rows || x >= cols) return;
    cell_put(x, y, c, attr);
}

void set_cursor(uint32_t y, uint32_t x) {
    if (y >= rows) y = rows - 1;
    if (x >= cols) x = cols - 1;
    row = (int)y;
    col = (int)x;
    update_cursor();
}

void clear_line(uint32_t y) {
    if (y >= rows) return;
    if (gfx) {
        hide_cursor();
        for (uint32_t x = 0; x < cols; x++) cells[y * cols + x] = vga_entry(' ', color);
        video_fill(off_x, off_y + y * cell_h, cols * cell_w, cell_h, video_color(color >> 4));
    } else {
        for (uint32_t x = 0; x < cols; x++) VGA_MEMORY[y * TEXT_COLS + x] = vga_entry(' ', color);
    }
}

void clear_screen(void) {
    if (gfx) {
        for (uint32_t i = 0; i < cols * rows; i++) cells[i] = vga_entry(' ', color);
        video_clear(video_color(color >> 4));   
        cur_x = cur_y = -1;
    } else {
        for (uint32_t i = 0; i < TEXT_COLS * TEXT_ROWS; i++) {
            VGA_MEMORY[i] = vga_entry(' ', color);
        }
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
            cell_put((uint32_t)col, (uint32_t)row, ' ', color);
        }
    } else {
        cell_put((uint32_t)col, (uint32_t)row, c, color);
        col++;
    }

    if (col >= (int)cols) {
        col = 0;
        row++;
    }
    if (row >= (int)rows) {
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
    while (i > 0) {
        print_char(buf[--i]);
    }
}

void terminal_print_video_info(void) {
    const struct video_info *v = video_get();
    print_string("Video: ");
    if (v->graphics) {
        print_dec(v->width);
        print_char('x');
        print_dec(v->height);
        print_char('x');
        print_dec(v->bpp);
        print_string(" framebuffer at ");
        print_hex(v->fb_addr);
        print_string(" (");
        print_string(v->source);
        print_string("), ");
    } else {
        print_string(v->source);
        print_string(", ");
    }
    print_dec(cols);
    print_char('x');
    print_dec(rows);
    print_string(" text cells");
    if (gfx) {
        print_string(", font ");
        print_dec(scale2 / 2);
        if (scale2 & 1) print_string(".5");
        print_char('x');
    }
    print_char('\n');
}
