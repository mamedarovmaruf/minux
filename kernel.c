#include "terminal.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "ata.h"
#include "fat.h"
#include "mos.h"
#include "video.h"
#include "pmm.h"
#include "paging.h"
#include "kheap.h"
#include "gdt.h"
#include "pit.h"

void kernel_main(uint32_t mb_magic, uint32_t mb_info) {
    gdt_init();                      /* own GDT before anything else */
    video_init(mb_magic, mb_info);   
    terminal_init();
    clear_screen();

    set_color(VGA_WHITE, VGA_BLACK);
    print_string("MINUX\n");

    set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    print_string("Kernel loaded successfully.\n");
    terminal_print_video_info();

    pic_init();
    idt_init();
    print_string("IDT loaded, exception handlers active.\n");

    /* Memory: needs IDT first so a page fault shows a panic screen. */
    pmm_init(mb_magic, mb_info);
    if (paging_init() == 0) {
        kheap_init();
        print_dec(pmm_free_pages() * 4 / 1024);
        print_string(" MB free, paging and heap ready.\n");
    } else {
        set_color(VGA_YELLOW, VGA_BLACK);
        print_string("Paging disabled: no memory map.\n");
        set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    }

    keyboard_init();
    pit_init(100);                   /* 100 Hz system tick */
    __asm__ volatile ("sti");
    print_string("Keyboard driver ready.\n");
    print_string("GDT loaded, timer running at ");
    print_dec(pit_hz());
    print_string(" Hz.\n");

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
