#include <stddef.h>
#include "video.h"
#include "font8x16.h"
#include "io.h"

#define MB_INFO_FRAMEBUFFER (1u << 12)

#define MB_FB_INDEXED 0
#define MB_FB_RGB     1
#define MB_FB_TEXT    2

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower, mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count, mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length, mmap_addr;
    uint32_t drives_length, drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    uint64_t fb_addr;
    uint32_t fb_pitch, fb_width, fb_height;
    uint8_t  fb_bpp, fb_type;
    uint8_t  color_info[6];   
} __attribute__((packed));

_Static_assert(offsetof(struct multiboot_info, vbe_control_info) == 72, "multiboot layout");
_Static_assert(offsetof(struct multiboot_info, fb_addr) == 88, "multiboot layout");
_Static_assert(offsetof(struct multiboot_info, fb_bpp) == 108, "multiboot layout");

#define BGA_PORT_INDEX 0x01CE
#define BGA_PORT_DATA  0x01CF

#define BGA_INDEX_ID          0
#define BGA_INDEX_XRES        1
#define BGA_INDEX_YRES        2
#define BGA_INDEX_BPP         3
#define BGA_INDEX_ENABLE      4
#define BGA_INDEX_VIRT_WIDTH  6

#define BGA_DISABLED     0x00
#define BGA_ENABLED      0x01
#define BGA_GETCAPS      0x02   
#define BGA_LFB_ENABLED  0x40

#define BGA_LEGACY_LFB   0xE0000000u

#define PCI_ADDR 0x0CF8
#define PCI_DATA 0x0CFC

#define TEXT_MODE_DEFAULTS {                                         \
    .graphics = 0, .bpp = 0, .bytes = 0,                             \
    .fb_addr = 0xB8000, .pitch = 160, .width = 80, .height = 25,     \
    .source = "VGA text mode (fallback)",                            \
}

static struct video_info vi = TEXT_MODE_DEFAULTS;

static uint32_t palette[16];

static const uint8_t vga_rgb[16][3] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
    {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
    {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF},
};

void video_copy(void *dst, const void *src, uint32_t bytes) {
    uint32_t dwords = bytes >> 2;
    uint32_t tail = bytes & 3;
    __asm__ volatile ("rep movsl"
                      : "+D"(dst), "+S"(src), "+c"(dwords)
                      :
                      : "memory");
    __asm__ volatile ("rep movsb"
                      : "+D"(dst), "+S"(src), "+c"(tail)
                      :
                      : "memory");
}

static void fill_dwords(void *dst, uint32_t value, uint32_t count) {
    __asm__ volatile ("rep stosl"
                      : "+D"(dst), "+c"(count)
                      : "a"(value)
                      : "memory");
}

static inline uint8_t *row_ptr(uint32_t y) {
    return (uint8_t *)(uintptr_t)vi.fb_addr + (uintptr_t)y * vi.pitch;
}

static void store_pixels(uint8_t *dst, const uint32_t *src, uint32_t n) {
    switch (vi.bytes) {
    case 4:
        video_copy(dst, src, n * 4);
        break;
    case 3:
        for (uint32_t i = 0; i < n; i++) {
            dst[0] = (uint8_t)src[i];
            dst[1] = (uint8_t)(src[i] >> 8);
            dst[2] = (uint8_t)(src[i] >> 16);
            dst += 3;
        }
        break;
    case 2:
        for (uint32_t i = 0; i < n; i++) {
            ((volatile uint16_t *)(void *)dst)[i] = (uint16_t)src[i];
        }
        break;
    default:
        break;
    }
}

static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t px = 0;
    if (vi.red_size)   px |= (uint32_t)(r >> (8 - (vi.red_size   > 8 ? 8 : vi.red_size)))   << vi.red_pos;
    if (vi.green_size) px |= (uint32_t)(g >> (8 - (vi.green_size > 8 ? 8 : vi.green_size))) << vi.green_pos;
    if (vi.blue_size)  px |= (uint32_t)(b >> (8 - (vi.blue_size  > 8 ? 8 : vi.blue_size)))  << vi.blue_pos;
    return px;
}

static void build_palette(void) {
    for (int i = 0; i < 16; i++) {
        palette[i] = pack_rgb(vga_rgb[i][0], vga_rgb[i][1], vga_rgb[i][2]);
    }
}

static int init_from_multiboot(const struct multiboot_info *mbi) {
    if (!(mbi->flags & MB_INFO_FRAMEBUFFER)) {
        return 0;
    }
    if (mbi->fb_type == MB_FB_TEXT) {
        vi.source = "VGA text mode (Multiboot)";
        return 0;
    }
    if (mbi->fb_type != MB_FB_RGB) {
        return 0;                           
    }
    if ((mbi->fb_addr >> 32) != 0) {
        return 0;                           
    }

    uint32_t bpp = mbi->fb_bpp;
    if (bpp != 32 && bpp != 24 && bpp != 16 && bpp != 15) {
        return 0;
    }
    uint32_t bytes = (bpp + 7) / 8;
    if (mbi->fb_width == 0 || mbi->fb_height == 0 ||
        mbi->fb_pitch < mbi->fb_width * bytes) {
        return 0;
    }

    vi.graphics   = 1;
    vi.bpp        = (uint8_t)bpp;
    vi.bytes      = (uint8_t)bytes;
    vi.fb_addr    = (uint32_t)mbi->fb_addr;
    vi.pitch      = mbi->fb_pitch;
    vi.width      = mbi->fb_width;
    vi.height     = mbi->fb_height;
    vi.red_pos    = mbi->color_info[0];
    vi.red_size   = mbi->color_info[1];
    vi.green_pos  = mbi->color_info[2];
    vi.green_size = mbi->color_info[3];
    vi.blue_pos   = mbi->color_info[4];
    vi.blue_size  = mbi->color_info[5];
    vi.source     = "Multiboot framebuffer";
    return 1;
}

static void bga_write(uint16_t index, uint16_t value) {
    outw(BGA_PORT_INDEX, index);
    outw(BGA_PORT_DATA, value);
}

static uint16_t bga_read(uint16_t index) {
    outw(BGA_PORT_INDEX, index);
    return inw(BGA_PORT_DATA);
}

static uint32_t pci_read(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off) {
    outl(PCI_ADDR, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFC));
    return inl(PCI_DATA);
}

static void pci_write(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off, uint32_t v) {
    outl(PCI_ADDR, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFC));
    outl(PCI_DATA, v);
}

static uint32_t bga_find_lfb(void) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read(bus, dev, 0, 0);
            if (id != 0x11111234u && id != 0xBEEF80EEu) {
                continue;                   
            }
            uint32_t bar0 = pci_read(bus, dev, 0, 0x10);
            if (bar0 & 1) {
                continue;                   
            }
            if (((bar0 >> 1) & 3) == 2 && pci_read(bus, dev, 0, 0x14) != 0) {
                continue;                   
            }
            uint32_t cmd = pci_read(bus, dev, 0, 0x04);
            if (!(cmd & 2)) {
                pci_write(bus, dev, 0, 0x04, cmd | 2);   
            }
            return bar0 & ~0xFu;
        }
    }
    return BGA_LEGACY_LFB;
}

static int init_from_bga(void) {
    uint16_t id = bga_read(BGA_INDEX_ID);
    if (id < 0xB0C0 || id > 0xB0CF) {
        return 0;
    }

    
    bga_write(BGA_INDEX_ENABLE, BGA_DISABLED);
    bga_write(BGA_INDEX_ENABLE, BGA_GETCAPS);
    uint32_t max_w = bga_read(BGA_INDEX_XRES);
    uint32_t max_h = bga_read(BGA_INDEX_YRES);
    bga_write(BGA_INDEX_ENABLE, BGA_DISABLED);
    if (max_w < 640 || max_h < 480) {
        max_w = max_h = 0xFFFF;
    }

    
    static const uint16_t modes[][2] = {
        {1920, 1080}, {1600, 900}, {1280, 720}, {1024, 768}, {800, 600},
    };

    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        uint16_t w = modes[i][0], h = modes[i][1];
        if (w > max_w || h > max_h) {
            continue;
        }

        bga_write(BGA_INDEX_ENABLE, BGA_DISABLED);
        bga_write(BGA_INDEX_XRES, w);
        bga_write(BGA_INDEX_YRES, h);
        bga_write(BGA_INDEX_BPP, 32);
        bga_write(BGA_INDEX_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);

        if (bga_read(BGA_INDEX_XRES) != w || bga_read(BGA_INDEX_YRES) != h ||
            bga_read(BGA_INDEX_BPP) != 32) {
            continue;
        }

        uint32_t virt = bga_read(BGA_INDEX_VIRT_WIDTH);
        if (virt < w) {
            virt = w;
        }

        vi.graphics   = 1;
        vi.bpp        = 32;
        vi.bytes      = 4;
        vi.fb_addr    = bga_find_lfb();
        vi.pitch      = virt * 4;
        vi.width      = w;
        vi.height     = h;
        vi.red_pos    = 16; vi.red_size   = 8;
        vi.green_pos  = 8;  vi.green_size = 8;
        vi.blue_pos   = 0;  vi.blue_size  = 8;
        vi.source     = "Bochs VBE (BGA)";
        return 1;
    }

    bga_write(BGA_INDEX_ENABLE, BGA_DISABLED);   
    return 0;
}

void video_init(uint32_t mb_magic, uint32_t mb_info) {
    int ok = 0;

    vi = (struct video_info)TEXT_MODE_DEFAULTS;

    if (mb_magic == MULTIBOOT_BOOTLOADER_MAGIC && mb_info != 0) {
        ok = init_from_multiboot((const struct multiboot_info *)(uintptr_t)mb_info);
    }
    if (!ok) {
        ok = init_from_bga();
    }
    if (ok) {
        build_palette();
    }
}

const struct video_info *video_get(void) {
    return &vi;
}

uint32_t video_color(uint8_t vga_index) {
    return palette[vga_index & 0x0F];
}

void video_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t px) {
    if (!vi.graphics || x >= vi.width || y >= vi.height) {
        return;
    }
    if (x + w > vi.width)  w = vi.width - x;
    if (y + h > vi.height) h = vi.height - y;

    if (vi.bytes == 4) {
        for (uint32_t r = 0; r < h; r++) {
            fill_dwords(row_ptr(y + r) + x * 4, px, w);
        }
        return;
    }

    
    uint8_t *first = row_ptr(y) + x * vi.bytes;
    uint32_t line[64];
    for (uint32_t done = 0; done < w; done += 64) {
        uint32_t n = w - done < 64 ? w - done : 64;
        for (uint32_t i = 0; i < n; i++) line[i] = px;
        store_pixels(first + done * vi.bytes, line, n);
    }
    for (uint32_t r = 1; r < h; r++) {
        video_copy(row_ptr(y + r) + x * vi.bytes, first, w * vi.bytes);
    }
}

void video_clear(uint32_t px) {
    video_fill(0, 0, vi.width, vi.height, px);
}

void video_glyph(uint32_t x, uint32_t y, uint8_t ch, uint32_t fg, uint32_t bg,
                 uint32_t scale2, int underline) {
    uint32_t line[4 * 2 * VIDEO_MAX_SCALE];

    if (!vi.graphics || scale2 < 2 || scale2 > 2 * VIDEO_MAX_SCALE) {
        return;
    }
    uint32_t w = 4 * scale2;              
    uint32_t h = 8 * scale2;              
    if (x + w > vi.width || y + h > vi.height) {
        return;
    }

    uint32_t last_gy = 0xFFFFFFFFu;
    for (uint32_t dy = 0; dy < h; dy++) {
        uint32_t gy = dy * 2 / scale2;    
        if (gy != last_gy) {
            uint8_t bits = font8x16[ch][gy];
            if (underline && gy >= 14) {
                bits = 0xFF;
            }
            for (uint32_t dx = 0; dx < w; dx++) {
                uint32_t gx = dx * 2 / scale2;   
                line[dx] = (bits & (0x80 >> gx)) ? fg : bg;
            }
            last_gy = gy;
        }
        store_pixels(row_ptr(y + dy) + x * vi.bytes, line, w);
    }
}

void video_scroll(uint32_t top, uint32_t height, uint32_t shift) {
    if (!vi.graphics || shift == 0 || shift >= height || top + height > vi.height) {
        return;
    }
    uint32_t bytes = vi.width * vi.bytes;
    for (uint32_t r = 0; r < height - shift; r++) {
        video_copy(row_ptr(top + r), row_ptr(top + r + shift), bytes);
    }
}
