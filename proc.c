#include "proc.h"
#include "sched.h"
#include "paging.h"
#include "pmm.h"
#include "kstring.h"
#include "userbin.h"

/* Back [virt, virt+bytes) with fresh user pages; copy src_len bytes of src
   into the start (the rest stays zero: bss and stack). */
static int map_user(addrspace_t *as, uint32_t virt, uint32_t bytes, const uint8_t *src, uint32_t src_len) {
    for (uint32_t off = 0; off < bytes; off += PAGE_SIZE) {
        uint32_t phys = pmm_alloc();
        if (!phys) return -1;
        kmemset((void *)(uintptr_t)phys, 0, PAGE_SIZE);     /* RAM is identity mapped */
        if (paging_map(as, virt + off, phys, PG_USER | PG_WRITE) < 0) {
            pmm_free(phys);
            return -1;
        }
        if (src && off < src_len) {
            uint32_t n = src_len - off;
            if (n > PAGE_SIZE) n = PAGE_SIZE;
            kmemcpy((void *)(uintptr_t)phys, src + off, n);
        }
    }
    return 0;
}

int proc_spawn(const char *name) {
    const struct prog *p = prog_find(name);
    if (!p) return -1;

    uint32_t size = (uint32_t)(p->end - p->start);
    struct user_header h;
    if (size < sizeof(h)) return -1;
    kmemcpy(&h, p->start, sizeof(h));
    if (h.magic != USER_MAGIC || h.file_size > size || h.mem_size < h.file_size ||
        h.mem_size > USER_MAX_IMAGE || h.entry < USER_BASE || h.entry >= USER_BASE + h.mem_size)
        return -1;

    addrspace_t as;
    if (paging_space_create(&as) < 0) return -1;

    if (map_user(&as, USER_BASE, h.mem_size, p->start, h.file_size) < 0 ||
        map_user(&as, USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_SIZE, 0, 0) < 0) {
        paging_space_destroy(&as);
        return -1;
    }

    int pid = sched_new_task(name, &as, h.entry, USER_STACK_TOP);
    if (pid < 0) {
        paging_space_destroy(&as);
        return -1;
    }
    return pid;
}
