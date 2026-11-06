#include <stdint.h>
#include "idt.h"

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern uint32_t isr_stub_table[32];

static struct idt_entry idt[256];
static struct idt_ptr idtp;

static void set_gate(int n, uint32_t handler, uint16_t selector) {
    idt[n].base_low  = (uint16_t)(handler & 0xFFFF);
    idt[n].base_high = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[n].selector  = selector;
    idt[n].zero      = 0;
    idt[n].flags     = 0x8E;
}

void idt_init(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));

    for (int i = 0; i < 32; i++) {
        set_gate(i, isr_stub_table[i], cs);
    }

    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base  = (uint32_t)(uintptr_t)idt;
    __asm__ volatile ("lidt %0" : : "m"(idtp));
}
