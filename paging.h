#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PG_PRESENT 0x001u
#define PG_WRITE   0x002u
#define PG_USER    0x004u
#define PG_NOCACHE 0x010u

typedef struct addrspace {
    uint32_t *dir;          /* page directory (phys == virt, identity mapped) */
} addrspace_t;

#define KHEAP_START 0xC0000000u
#define KHEAP_MAX   0x01000000u                 /* 16 MB */

int  paging_init(void);                         /* identity-map RAM, enable paging; 0 = ok */
int  paging_map(addrspace_t *as, uint32_t virt, uint32_t phys, uint32_t flags);
void paging_unmap(addrspace_t *as, uint32_t virt);
uint32_t paging_phys(addrspace_t *as, uint32_t virt);   /* 0 if not mapped */
int  paging_map_range(addrspace_t *as, uint32_t virt, uint32_t phys, uint32_t bytes, uint32_t flags);

addrspace_t *paging_kernel_space(void);
int  paging_space_create(addrspace_t *out);     /* new space sharing kernel mappings */
void paging_space_destroy(addrspace_t *as);     /* frees user pages and tables */
void paging_switch(addrspace_t *as);

#endif
