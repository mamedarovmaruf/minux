#include "kstring.h"

uint32_t kstrlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

/* rep movsb instead of a C loop: at -O2 gcc may turn a plain loop into a
   call to memcpy, which does not exist in this kernel */
void kmemcpy(void *dst, const void *src, uint32_t n) {
    __asm__ volatile ("rep movsb"
                      : "+D"(dst), "+S"(src), "+c"(n)
                      :
                      : "memory");
}

void kmemset(void *dst, uint8_t value, uint32_t n) {
    __asm__ volatile ("rep stosb"
                      : "+D"(dst), "+c"(n)
                      : "a"(value)
                      : "memory");
}
