#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>

void  kheap_init(void);
void *kmalloc(uint32_t size);           /* 16-byte aligned, NULL on failure */
void *kcalloc(uint32_t size);           /* zeroed */
void  kfree(void *ptr);
void  kheap_stats(uint32_t *used, uint32_t *free_bytes, uint32_t *blocks);

#endif
