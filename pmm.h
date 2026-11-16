#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PAGE_SIZE 4096u

void     pmm_init(uint32_t mb_magic, uint32_t mb_info);
uint32_t pmm_alloc(void);               /* returns phys addr, 0 if out of memory */
void     pmm_free(uint32_t phys);
uint32_t pmm_total_pages(void);
uint32_t pmm_free_pages(void);
uint32_t pmm_max_phys(void);            /* end of the highest usable RAM we manage */

#endif
