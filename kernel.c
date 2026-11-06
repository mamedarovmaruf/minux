#include "terminal.h"
#include "idt.h"

void kernel_main(void) {
    clear_screen();

    set_color(VGA_WHITE, VGA_BLACK);
    print_string("MINUX\n");

    set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    print_string("Kernel loaded successfully.\n");

    idt_init();
    print_string("IDT loaded, exception handlers active.\n");

    set_color(VGA_LIGHT_GREY, VGA_BLACK);
    print_string("Video memory at ");
    print_hex(0xB8000);
    print_string("\n");

    while (1) {
        __asm__ volatile ("hlt");
    }
}
