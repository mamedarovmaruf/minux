#include "kheap.h"
#include "paging.h"
#include "pmm.h"
#include "kstring.h"
#include "panic.h"

#define ALIGN      16u
#define MAGIC_USED 0xA110C8EDu
#define MAGIC_FREE 0xF4EEB10Cu

/* Header sits right before each block; blocks are chained in address order. */
struct block {
    uint32_t magic;
    uint32_t size;              /* payload bytes */
    struct block *prev, *next;
};

#define HDR ((uint32_t)((sizeof(struct block) + ALIGN - 1) & ~(ALIGN - 1)))

static struct block *head, *tail;
static uint32_t heap_end;       /* first unmapped heap address */

/* Map more pages at the end of the heap and add them as a free block. */
static int grow(uint32_t min_payload) {
    uint32_t bytes = (min_payload + HDR + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (heap_end + bytes > KHEAP_START + KHEAP_MAX) return -1;

    for (uint32_t off = 0; off < bytes; off += PAGE_SIZE) {
        uint32_t phys = pmm_alloc();
        if (!phys || paging_map(paging_kernel_space(), heap_end + off, phys, PG_WRITE) < 0) {
            if (phys) pmm_free(phys);
            for (uint32_t u = 0; u < off; u += PAGE_SIZE) {     /* roll back */
                pmm_free(paging_phys(paging_kernel_space(), heap_end + u));
                paging_unmap(paging_kernel_space(), heap_end + u);
            }
            return -1;
        }
    }

    struct block *b = (struct block *)(uintptr_t)heap_end;
    b->magic = MAGIC_FREE;
    b->size = bytes - HDR;
    b->prev = tail;
    b->next = 0;
    if (tail) tail->next = b; else head = b;
    tail = b;
    heap_end += bytes;

    /* Merge with the previous block if it is free and adjacent. */
    if (b->prev && b->prev->magic == MAGIC_FREE) {
        b->prev->size += HDR + b->size;
        b->prev->next = 0;
        tail = b->prev;
    }
    return 0;
}

void kheap_init(void) {
    head = tail = 0;
    heap_end = KHEAP_START;
    grow(PAGE_SIZE - HDR);
}

void *kmalloc(uint32_t size) {
    if (!size) return 0;
    size = (size + ALIGN - 1) & ~(ALIGN - 1);

    for (int attempt = 0; attempt < 2; attempt++) {
        for (struct block *b = head; b; b = b->next) {
            if (b->magic != MAGIC_FREE || b->size < size) continue;

            /* Split if the rest can hold a header plus a minimal block. */
            if (b->size >= size + HDR + ALIGN) {
                struct block *n = (struct block *)((uint8_t *)b + HDR + size);
                n->magic = MAGIC_FREE;
                n->size = b->size - size - HDR;
                n->prev = b;
                n->next = b->next;
                if (b->next) b->next->prev = n; else tail = n;
                b->next = n;
                b->size = size;
            }
            b->magic = MAGIC_USED;
            return (uint8_t *)b + HDR;
        }
        if (grow(size) < 0) break;      /* out of heap or RAM */
    }
    return 0;
}

void *kcalloc(uint32_t size) {
    void *p = kmalloc(size);
    if (p) kmemset(p, 0, size);
    return p;
}

void kfree(void *ptr) {
    if (!ptr) return;
    struct block *b = (struct block *)((uint8_t *)ptr - HDR);
    if (b->magic != MAGIC_USED) {       /* double free or corruption */
        kernel_panic("kfree: invalid pointer");
    }
    b->magic = MAGIC_FREE;

    /* Coalesce with next, then with previous. */
    if (b->next && b->next->magic == MAGIC_FREE) {
        b->size += HDR + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b; else tail = b;
    }
    if (b->prev && b->prev->magic == MAGIC_FREE) {
        b->prev->size += HDR + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev; else tail = b->prev;
    }
}

void kheap_stats(uint32_t *used, uint32_t *free_bytes, uint32_t *blocks) {
    uint32_t u = 0, f = 0, n = 0;
    for (struct block *b = head; b; b = b->next, n++) {
        if (b->magic == MAGIC_USED) u += b->size; else f += b->size;
    }
    if (used) *used = u;
    if (free_bytes) *free_bytes = f;
    if (blocks) *blocks = n;
}
