#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

struct regs {
    uint32_t edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} __attribute__((packed));

void kernel_panic(const char *msg);
void isr_handler(struct regs *r);

#endif
