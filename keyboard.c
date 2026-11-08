#include <stdint.h>
#include "keyboard.h"
#include "irq.h"
#include "pic.h"
#include "io.h"

#define KBD_DATA 0x60
#define KBD_STATUS 0x64
#define BUF_SIZE 256

static const char map_normal[58] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const char map_shift[58] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

static volatile char buffer[BUF_SIZE];
static volatile uint32_t head = 0;
static volatile uint32_t tail = 0;

static int shift_down = 0;
static int ctrl_down = 0;
static int caps_on = 0;
static int extended = 0;

static void buffer_push(char c) {
    uint32_t next = (head + 1) & (BUF_SIZE - 1);
    if (next == tail) {
        return;
    }
    buffer[head] = c;
    head = next;
}

static void keyboard_irq(struct regs *r) {
    (void)r;
    uint8_t sc = inb(KBD_DATA);

    if (sc == 0xE0) {
        extended = 1;
        return;
    }
    if (extended) {
        extended = 0;
        return;
    }

    int released = sc & 0x80;
    uint8_t code = sc & 0x7F;

    switch (code) {
        case 0x2A:
        case 0x36:
            shift_down = !released;
            return;
        case 0x1D:
            ctrl_down = !released;
            return;
        case 0x3A:
            if (!released) {
                caps_on = !caps_on;
            }
            return;
        default:
            break;
    }

    if (released || code >= sizeof(map_normal)) {
        return;
    }

    char c = map_normal[code];
    if (!c) {
        return;
    }

    if (c >= 'a' && c <= 'z') {
        if (shift_down ^ caps_on) {
            c = (char)(c - 'a' + 'A');
        }
        if (ctrl_down) {
            c = (char)(c & 0x1F);
        }
    } else if (shift_down) {
        c = map_shift[code];
    }

    buffer_push(c);
}

void keyboard_init(void) {
    while (inb(KBD_STATUS) & 1) {
        (void)inb(KBD_DATA);
    }
    irq_install_handler(1, keyboard_irq);
    pic_unmask(1);
}

int keyboard_has_char(void) {
    return head != tail;
}

char keyboard_getchar(void) {
    for (;;) {
        __asm__ volatile ("cli");
        if (head != tail) {
            char c = buffer[tail];
            tail = (tail + 1) & (BUF_SIZE - 1);
            __asm__ volatile ("sti");
            return c;
        }
        __asm__ volatile ("sti; hlt");
    }
}
