#include "pmm.h"
#include "multiboot.h"
#include "video.h"
#include "kstring.h"

#define PMM_LIMIT   0xC0000000u
#define MAX_PAGES   (PMM_LIMIT / PAGE_SIZE)

extern char kernel_end;

static uint32_t bitmap[MAX_PAGES / 32];
static uint32_t total_pages, used_pages, max_phys, search_hint;

static void mark_used(uint32_t page) {
    if (!(bitmap[page / 32] & (1u << (page % 32)))) {
        bitmap[page / 32] |= 1u << (page % 32);
        used_pages++;
    }
}

static void mark_free(uint32_t page) {
    if (bitmap[page / 32] & (1u << (page % 32))) {
        bitmap[page / 32] &= ~(1u << (page % 32));
        used_pages--;
    }
}

static void reserve(uint32_t start, uint32_t end) {
    for (uint32_t p = start / PAGE_SIZE; p < (end + PAGE_SIZE - 1) / PAGE_SIZE && p < MAX_PAGES; p++)
        mark_used(p);
}

void pmm_init(uint32_t mb_magic, uint32_t mb_info) {
    kmemset(bitmap, 0xFF, sizeof(bitmap));
    used_pages = MAX_PAGES;

    const struct mb_info *mbi = (const struct mb_info *)(uintptr_t)mb_info;
    if (mb_magic != MULTIBOOT_BOOTLOADER_MAGIC || !mbi || !(mbi->flags & MB_INFO_MMAP))
        return;                                 

    uint32_t p = mbi->mmap_addr, end = mbi->mmap_addr + mbi->mmap_length;
    while (p < end) {
        const struct mb_mmap_entry *e = (const struct mb_mmap_entry *)(uintptr_t)p;
        if (e->type == 1 && e->addr < PMM_LIMIT) {
            uint64_t s = (e->addr + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
            uint64_t t = e->addr + e->len;
            if (t > PMM_LIMIT) t = PMM_LIMIT;
            for (; s + PAGE_SIZE <= t; s += PAGE_SIZE) {
                mark_free((uint32_t)(s / PAGE_SIZE));
                total_pages++;
            }
            if (t > max_phys) max_phys = (uint32_t)t;
        }
        p += e->size + 4;
    }

    reserve(0, (uint32_t)(uintptr_t)&kernel_end > 0x100000u ? (uint32_t)(uintptr_t)&kernel_end : 0x100000u);
    reserve(mb_info, mb_info + sizeof(*mbi));
    reserve(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length);
}

uint32_t pmm_alloc(void) {
    for (uint32_t n = 0; n < MAX_PAGES / 32; n++) {
        uint32_t i = (search_hint + n) % (MAX_PAGES / 32);
        if (bitmap[i] == 0xFFFFFFFFu) continue;
        for (uint32_t b = 0; b < 32; b++) {
            if (!(bitmap[i] & (1u << b))) {
                bitmap[i] |= 1u << b;
                used_pages++;
                search_hint = i;
                return (i * 32 + b) * PAGE_SIZE;
            }
        }
    }
    return 0;
}

void pmm_free(uint32_t phys) {
    uint32_t page = phys / PAGE_SIZE;
    if (page < MAX_PAGES) mark_free(page);
}

uint32_t pmm_total_pages(void) { return total_pages; }
uint32_t pmm_free_pages(void)  { return MAX_PAGES - used_pages; }
uint32_t pmm_max_phys(void)    { return max_phys; }
