#include "terminal.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "mos.h"

void kernel_main(void) {
    clear_screen();

    set_color(VGA_WHITE, VGA_BLACK);
    print_string("MINUX\n");

    set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    print_string("Kernel loaded successfully.\n");

    pic_init();
    idt_init();
    print_string("IDT loaded, exception handlers active.\n");

    keyboard_init();
    __asm__ volatile ("sti");
    print_string("Keyboard driver ready.\n\n");

    set_color(VGA_LIGHT_GREY, VGA_BLACK);
    mos_run();
}
