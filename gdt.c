#include <stdint.h>
#include "gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;          
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t prev, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap;
} __attribute__((packed));

#define GDT_COUNT 6

static struct gdt_entry gdt[GDT_COUNT];
static struct gdt_ptr gdtp;
static struct tss_entry tss;

static void set_entry(int n, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt[n].base_low   = (uint16_t)(base & 0xFFFF);
    gdt[n].base_mid   = (uint8_t)((base >> 16) & 0xFF);
    gdt[n].base_high  = (uint8_t)((base >> 24) & 0xFF);
    gdt[n].limit_low  = (uint16_t)(limit & 0xFFFF);
    gdt[n].gran       = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    gdt[n].access     = access;
}

void tss_set_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

void gdt_init(void) {
    set_entry(0, 0, 0,          0x00, 0x00);    
    set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);  
    set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);    
    set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);    
    set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);    
    tss.ss0    = GDT_KERNEL_DS;
    tss.iomap  = sizeof(tss);       
    set_entry(5, (uint32_t)(uintptr_t)&tss, sizeof(tss) - 1, 0x89, 0x00);

    gdtp.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtp.base  = (uint32_t)(uintptr_t)gdt;

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

    __asm__ volatile ("ltr %0" : : "r"((uint16_t)GDT_TSS));
}
