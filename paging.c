#include "paging.h"
#include "pmm.h"
#include "video.h"
#include "kstring.h"

#define PDE_COUNT   1024

static addrspace_t kernel_as;

static inline void invlpg(uint32_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

static uint32_t *new_table(void) {
    uint32_t phys = pmm_alloc();
    if (!phys) return 0;
    kmemset((void *)phys, 0, PAGE_SIZE);    /* RAM is identity mapped */
    return (uint32_t *)phys;
}

/* Returns the PTE slot for virt, creating the table if asked to. */
static uint32_t *pte_slot(addrspace_t *as, uint32_t virt, int create, uint32_t flags) {
    uint32_t *pde = &as->dir[virt >> 22];
    if (!(*pde & PG_PRESENT)) {
        if (!create) return 0;
        uint32_t *t = new_table();
        if (!t) return 0;
        *pde = (uint32_t)(uintptr_t)t | PG_PRESENT | PG_WRITE | (flags & PG_USER);
    } else if (flags & PG_USER) {
        *pde |= PG_USER;
    }
    uint32_t *table = (uint32_t *)(uintptr_t)(*pde & ~0xFFFu);
    return &table[(virt >> 12) & 0x3FF];
}

/* A user space must not touch tables it shares with the kernel. */
static int is_kernel_region(addrspace_t *as, uint32_t virt) {
    uint32_t k = kernel_as.dir[virt >> 22];
    return as != &kernel_as && (k & PG_PRESENT) && as->dir[virt >> 22] == k;
}

int paging_map(addrspace_t *as, uint32_t virt, uint32_t phys, uint32_t flags) {
    if (is_kernel_region(as, virt)) return -1;
    uint32_t *pte = pte_slot(as, virt, 1, flags);
    if (!pte) return -1;
    *pte = (phys & ~0xFFFu) | (flags & 0xFFFu) | PG_PRESENT;
    invlpg(virt);
    return 0;
}

void paging_unmap(addrspace_t *as, uint32_t virt) {
    uint32_t *pte = pte_slot(as, virt, 0, 0);
    if (pte && (*pte & PG_PRESENT)) {
        *pte = 0;
        invlpg(virt);
    }
}

uint32_t paging_phys(addrspace_t *as, uint32_t virt) {
    uint32_t *pte = pte_slot(as, virt, 0, 0);
    if (!pte || !(*pte & PG_PRESENT)) return 0;
    return (*pte & ~0xFFFu) | (virt & 0xFFFu);
}

int paging_map_range(addrspace_t *as, uint32_t virt, uint32_t phys, uint32_t bytes, uint32_t flags) {
    uint32_t off = phys & 0xFFFu;
    virt &= ~0xFFFu;
    phys &= ~0xFFFu;
    for (uint32_t i = 0; i < bytes + off; i += PAGE_SIZE)
        if (paging_map(as, virt + i, phys + i, flags) < 0) return -1;
    return 0;
}

addrspace_t *paging_kernel_space(void) { return &kernel_as; }

void paging_switch(addrspace_t *as) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(as->dir) : "memory");
}

int paging_init(void) {
    if (!pmm_max_phys()) return -1;             /* no memory map, stay unpaged */
    kernel_as.dir = new_table();
    if (!kernel_as.dir) return -1;

    /* Identity map all RAM, supervisor only. Page 0 stays unmapped so
       NULL dereferences fault. */
    uint32_t end = (pmm_max_phys() + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint32_t a = PAGE_SIZE; a < end; a += PAGE_SIZE)
        paging_map(&kernel_as, a, a, PG_WRITE);
    paging_map(&kernel_as, 0xB8000, 0xB8000, PG_WRITE);     /* VGA text */

    /* Framebuffer lives in device memory above RAM. */
    const struct video_info *vi = video_get();
    if (vi->graphics && vi->fb_addr >= end)
        paging_map_range(&kernel_as, vi->fb_addr, vi->fb_addr,
                         vi->pitch * vi->height, PG_WRITE | PG_NOCACHE);

    /* Create heap tables now so every address space shares them. */
    for (uint32_t a = KHEAP_START; a < KHEAP_START + KHEAP_MAX; a += 0x400000u)
        if (!pte_slot(&kernel_as, a, 1, 0)) return -1;

    paging_switch(&kernel_as);
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80010000u;                 /* PG | WP (kernel honours read-only pages) */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    return 0;
}

int paging_space_create(addrspace_t *out) {
    uint32_t *dir = new_table();
    if (!dir) return -1;
    /* Share kernel page tables: same PDEs, so kernel changes show everywhere. */
    for (uint32_t i = 0; i < PDE_COUNT; i++)
        dir[i] = kernel_as.dir[i];
    out->dir = dir;
    return 0;
}

void paging_space_destroy(addrspace_t *as) {
    /* Only tables with PG_USER were created for this space; kernel ones are shared. */
    for (uint32_t i = 0; i < PDE_COUNT; i++) {
        uint32_t pde = as->dir[i];
        if (pde == kernel_as.dir[i] || !(pde & PG_PRESENT)) continue;
        uint32_t *table = (uint32_t *)(uintptr_t)(pde & ~0xFFFu);
        for (uint32_t j = 0; j < 1024; j++)
            if (table[j] & PG_PRESENT) pmm_free(table[j] & ~0xFFFu);
        pmm_free((uint32_t)(uintptr_t)table);
    }
    pmm_free((uint32_t)(uintptr_t)as->dir);
    as->dir = 0;
}
