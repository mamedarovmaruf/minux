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
