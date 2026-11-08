#ifndef IO_H
#define IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void insw(uint16_t port, void *addr, uint32_t count) {
    __asm__ volatile ("rep insw"
                      : "+D"(addr), "+c"(count)
                      : "d"(port)
                      : "memory");
}

static inline void outsw(uint16_t port, const void *addr, uint32_t count) {
    __asm__ volatile ("rep outsw"
                      : "+S"(addr), "+c"(count)
                      : "d"(port)
                      : "memory");
}

#endif
