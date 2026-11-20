#include "irq.h"
#include "pic.h"
#include "sched.h"

static irq_handler_t handlers[16];

void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        handlers[irq] = handler;
    }
}

uint32_t irq_handler(struct regs *r) {
    int irq = (int)r->int_no - 32;
    if (irq >= 0 && irq < 16 && handlers[irq]) {
        handlers[irq](r);
    }
    pic_send_eoi((uint8_t)irq);

    /* The timer tick is where tasks get switched. */
    if (irq == 0) {
        return sched_tick(r);
    }
    return (uint32_t)(uintptr_t)r;
}
