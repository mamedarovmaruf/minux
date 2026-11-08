#ifndef KSTRING_H
#define KSTRING_H

#include <stdint.h>

uint32_t kstrlen(const char *s);
int kstrcmp(const char *a, const char *b);
void kmemcpy(void *dst, const void *src, uint32_t n);
void kmemset(void *dst, uint8_t value, uint32_t n);

#endif
