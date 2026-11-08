#include "irq.h"
#include "pic.h"

static irq_handler_t handlers[16];

void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        handlers[irq] = handler;
    }
}

void irq_handler(struct regs *r) {
    int irq = (int)r->int_no - 32;
    if (irq >= 0 && irq < 16 && handlers[irq]) {
        handlers[irq](r);
    }
    pic_send_eoi((uint8_t)irq);
}
