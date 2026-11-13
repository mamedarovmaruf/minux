#include "terminal.h"
#include "panic.h"

static const char *exception_names[32] = {
    "Division Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection",
    "VMM Communication",
    "Security Exception",
    "Reserved"
};

static uint32_t read_cr2(void) {
    uint32_t value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* ---- Gremlin mascot (bottom-right corner), drawn with the letter 'M' ----
 * K = outline/eyes (black), W = fur (white), G = inner ear (dark grey),
 * space = transparent (red background shows through).
 * Edit the table to redraw him. */
#define GREMLIN_W    38
#define GREMLIN_H    19
#define GREMLIN_ROW  6    /* 6 + 19 = 25 -> last screen row */
#define GREMLIN_COL  42   /* 42 + 38 = 80 -> last screen column */

static const char *gremlin_art[GREMLIN_H] = {
    "KK                 KK             KKK ",
    "KGKK              KWWK         KKKGGK ",
    "KGGGKK           KWWWWK     KKKGGGGGK ",
    " KGGGGKK        KWWWWWWK  KKGGGGGGGGK ",
    " KGGGGGGKK    KKWWWWWWWWKKGGGGGGGGGK  ",
    "  KGGGGGGKKKKKWWWWWWWWWWWKKGGGGGGGK   ",
    "   KGGGGGGKWWWWWWWWWWWWWWWKGGGGGGKK   ",
    "    KGGGGKWWWWWWWWWWWWWWWWWKGGGGK     ",
    "     KKGKWWWKKKKWWWWKKKKWWWWKGKK      ",
    "       KKWWKKWWKWWWWKWWKKWWWWKK       ",
    "        KWWKKKKKWWWWKKKKKWWWWK        ",
    "        KWWWKKKKWWKWWKKKKWWWWK        ",
    "        KKWWWWWWWKKKKWWWWWWWKK        ",
    "       KKWWKWWWWWWWKKWWWWWWKWWK       ",
    "      KWWKWWKWWWWWWWWWWWWKWWKWWK      ",
    "     KWWWKWWWKWWWWWWWWWWKWWWKWWWKK    ",
    "    KWWWWWKWWWWKWWWWWWKWWWWKWWWWWK    ",
    "    KWWKWWWKKWWWWKKWWWWKKWWWWKKWWK    ",
    "     KKKKKKKKKKKKKKKKKKKKKKKKKKKK     ",
};

static void draw_gremlin(void) {
    for (int y = 0; y < GREMLIN_H; y++) {
        for (int x = 0; x < GREMLIN_W; x++) {
            char c = gremlin_art[y][x];
            uint8_t fg;
            if      (c == 'K') fg = VGA_BLACK;
            else if (c == 'W') fg = VGA_WHITE;
            else if (c == 'G') fg = VGA_DARK_GREY;
            else continue;
            put_char_at((uint8_t)(GREMLIN_ROW + y), (uint8_t)(GREMLIN_COL + x),
                        'M', (uint8_t)(fg | (VGA_RED << 4)));
        }
    }
}

static void panic_screen(const char *msg) {
    set_color(VGA_WHITE, VGA_RED);
    clear_screen();
    print_string("KERNEL PANIC: ");
    print_string(msg);
    print_string("\n\n");
    draw_gremlin();
}

static void print_reg(const char *name, uint32_t value) {
    print_string(name);
    print_string(": ");
    print_hex(value);
    print_string("  ");
}

void kernel_panic(const char *msg) {
    __asm__ volatile ("cli");
    panic_screen(msg);
    print_string("System halted.");
    halt_forever();
}

void isr_handler(struct regs *r) {
    __asm__ volatile ("cli");
    panic_screen("unhandled CPU exception");

    print_string("Exception: ");
    print_string(r->int_no < 32 ? exception_names[r->int_no] : "Unknown");
    print_string(" (#");
    print_dec(r->int_no);
    print_string(")\n");

    print_reg("Error code", r->err_code);
    print_string("\n");

    print_reg("EIP", r->eip);
    print_reg("CS", r->cs);
    print_reg("EFLAGS", r->eflags);
    print_string("\n\n");

    print_reg("EAX", r->eax);
    print_reg("EBX", r->ebx);
    print_string("\n");
    print_reg("ECX", r->ecx);
    print_reg("EDX", r->edx);
    print_string("\n");
    print_reg("ESI", r->esi);
    print_reg("EDI", r->edi);
    print_string("\n");
    print_reg("EBP", r->ebp);
    print_string("\n");

    if (r->int_no == 14) {
        print_string("\nPage fault at address ");
        print_hex(read_cr2());
        print_string("\n  ");
        print_string((r->err_code & 1) ? "protection violation" : "page not present");
        print_string("\n  ");
        print_string((r->err_code & 2) ? "write" : "read");
        print_string(", ");
        print_string((r->err_code & 4) ? "user mode" : "kernel mode");
        print_string("\n");
    }

    print_string("\nSystem halted.");
    halt_forever();
}
