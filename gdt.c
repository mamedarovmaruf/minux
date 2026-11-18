#include <stdint.h>
#include "gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;          /* high nibble: flags, low nibble: limit 19..16 */
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define GDT_COUNT 5

static struct gdt_entry gdt[GDT_COUNT];
static struct gdt_ptr gdtp;

static void set_entry(int n, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt[n].base_low   = (uint16_t)(base & 0xFFFF);
    gdt[n].base_mid   = (uint8_t)((base >> 16) & 0xFF);
    gdt[n].base_high  = (uint8_t)((base >> 24) & 0xFF);
    gdt[n].limit_low  = (uint16_t)(limit & 0xFFFF);
    gdt[n].gran       = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    gdt[n].access     = access;
}

void gdt_init(void) {
    /* Flat model: every segment covers the whole 4 GB. */
    set_entry(0, 0, 0,          0x00, 0x00);    /* null */
    set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);    /* kernel code, ring 0 */
    set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);    /* kernel data, ring 0 */
    set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);    /* user code, ring 3 */
    set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);    /* user data, ring 3 */

    gdtp.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtp.base  = (uint32_t)(uintptr_t)gdt;

    /* Load it, then reload every segment register (CS needs a far jump). */
    __asm__ volatile (
        "lgdt %0\n\t"
        "mov %1, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "ljmp %2, $1f\n\t"
        "1:"
        :
        : "m"(gdtp), "i"(GDT_KERNEL_DS), "i"(GDT_KERNEL_CS)
        : "eax", "memory"
    );
}
