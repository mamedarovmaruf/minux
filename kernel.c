#include "terminal.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "ata.h"
#include "fat.h"
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
    print_string("Keyboard driver ready.\n");

    if (ata_init() == 0) {
        print_string("Disk found: ");
        print_dec(ata_sector_count() / 2048);
        print_string(" MB\n");

        int r = fat_mount();
        if (r == FAT_OK) {
            print_string("FAT16 filesystem mounted.\n");
        } else {
            set_color(VGA_YELLOW, VGA_BLACK);
            print_string(fat_strerror(r));
            print_string("\n");
        }
    } else {
        set_color(VGA_YELLOW, VGA_BLACK);
        print_string("No disk found.\n");
    }
    print_string("\n");

    set_color(VGA_LIGHT_GREY, VGA_BLACK);
    mos_run();
}
