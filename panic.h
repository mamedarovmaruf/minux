#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

/* Stack frame built by isr.asm. useresp/ss exist only for ring 3 -> 0. */
struct regs {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
    uint32_t useresp, ss;
} __attribute__((packed));

void kernel_panic(const char *msg);
/* Handlers return the stack to resume on (the scheduler may change it). */
uint32_t isr_handler(struct regs *r);

#endif
