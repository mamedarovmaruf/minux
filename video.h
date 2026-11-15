#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define VIDEO_MAX_SCALE 4

struct video_info {
    uint8_t     graphics;      
    uint8_t     bpp;           
    uint8_t     bytes;         
    uint8_t     red_pos,   red_size;
    uint8_t     green_pos, green_size;
    uint8_t     blue_pos,  blue_size;
    uint32_t    fb_addr;       
    uint32_t    pitch;         
    uint32_t    width;         
    uint32_t    height;        
    const char *source;        
};

void video_init(uint32_t mb_magic, uint32_t mb_info);

const struct video_info *video_get(void);

uint32_t video_color(uint8_t vga_index);

void video_clear(uint32_t px);
void video_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t px);

void video_glyph(uint32_t x, uint32_t y, uint8_t ch, uint32_t fg, uint32_t bg,
                 uint32_t scale2, int underline);

void video_scroll(uint32_t top, uint32_t height, uint32_t shift);

void video_copy(void *dst, const void *src, uint32_t bytes);

#endif
